#ifndef NATIVE_TEST

#include "relay.h"
#include <Arduino.h>

void Relay::begin() {
    _lastBeacon = millis();
    loadSeq();
    _lastSentHash = _store.stateHash(millis());
}

void Relay::loadSeq() {
    _prefs.begin("relay", false);
    _nextSeq = _prefs.getUShort("seq", 1);
    _bootCount = _prefs.getULong("boot", 0) + 1;
    _prefs.putULong("boot", _bootCount);
}

void Relay::saveSeq() {
    _prefs.putUShort("seq", _nextSeq);
}

void Relay::process() {
    uint32_t now = millis();

    // 1. Receive from LoRa
    pollLoRa();

    // 2. Process BLE commands
    pollBle();

    // 3. Continue active sync session
    if (_sync.active) {
        if (now - _sync.lastActivityMs > SYNC_TIMEOUT_MS) {
            Serial.println("[Relay] Sync session timed out");
            resetSyncSession();
        } else {
            continueSyncSend();
        }
    }

    // 4. Chunked BLE message streaming (one per loop)
    if (_bleGetMsg.active) {
        const SenderLog* sender = _store.getSender(_bleGetMsg.senderId);
        if (sender && _bleGetMsg.currentSeq <= _bleGetMsg.endSeq) {
            const MessageEntry* entry = sender->getMessage(_bleGetMsg.currentSeq);
            if (entry) {
                blePushMessage(*entry);
            }
            _bleGetMsg.currentSeq++;
        } else {
            _bleGetMsg.active = false;
        }
    }

    // 5. Beacon timer
    if (now - _lastBeacon >= BEACON_INTERVAL_MS) {
        _lastBeacon = now;
        sendBeacon();
    }

    // 6. Check presence transitions
    checkPresence();

    // 7. Purge expired senders
    _store.purgeExpired(now);
}

void Relay::onBleReceive(const uint8_t* data, size_t len) {
    if (len > BLE_RX_BUF_SIZE || len == 0) return;
    if (_bleRxReady) return;
    memcpy(_bleRxBuf, data, len);
    _bleRxLen = len;
    _bleRxReady = true;
}

uint32_t Relay::currentTimestamp() const {
    int32_t t = static_cast<int32_t>(millis() / 1000) + _clockOffset;
    return (t > 0) ? static_cast<uint32_t>(t) : 0;
}

// --- LoRa ---

void Relay::pollLoRa() {
    uint8_t buf[MAX_MSG_PAYLOAD + MESSAGE_HEADER_SIZE];
    size_t len = _lora.receive(buf, sizeof(buf));
    if (len > 0) {
        handleLoRaMessage(buf, len);
    }
}

void Relay::handleLoRaMessage(const uint8_t* buf, size_t len) {
    Message msg;
    if (!msg.decode(buf, len)) {
        Serial.println("[Relay] Failed to decode LoRa message");
        return;
    }

    switch (msg.msgType) {
        case MsgType::Location:
        case MsgType::Text:
            handleLiveMessage(msg);
            break;
        case MsgType::Beacon:
            handleBeacon(msg);
            break;
        case MsgType::Digest:
            handleDigest(msg);
            break;
        case MsgType::SyncRequest:
            handleSyncRequest(msg);
            break;
    }
}

void Relay::handleLiveMessage(Message& msg) {
    uint32_t now = millis();
    MessageEntry entry = msg.toEntry();
    bool stored = _store.storeMessage(entry, now);

    if (!stored) {
        // Duplicate — already have this (senderId, seq)
        return;
    }

    Serial.printf("[Relay] Live msg sender=%d seq=%d type=%d\n",
        msg.senderId, msg.seq, static_cast<int>(msg.msgType));

    // Push to BLE app if connected
    if (_ble.isConnected()) {
        blePushMessage(entry);
    }

    // Update state hash
    _lastSentHash = _store.stateHash(millis());
}

void Relay::handleBeacon(const Message& msg) {
    uint32_t now = millis();
    uint16_t peerHash = msg.beaconStateHash();
    uint16_t ourHash = _store.stateHash(now);

    Serial.printf("[Relay] Beacon from %d hash=0x%04X (ours=0x%04X)\n",
        msg.senderId, peerHash, ourHash);

    _peerHash = peerHash;

    // Update presence for known senders (don't create new entries for unknown retranslators)
    _store.updatePresence(msg.senderId, now);

    // If hashes differ and no active sync, send our digest
    if (peerHash != ourHash && !_sync.active) {
        // Send our digest as a LoRa message
        uint8_t digestPayload[1 + MAX_SENDERS * 5];
        size_t dLen = _store.encodeDigest(digestPayload, sizeof(digestPayload), now);
        if (dLen > 0) {
            Message digestMsg;
            digestMsg.senderId = DEVICE_ID;
            digestMsg.seq = 0;
            digestMsg.timestamp = 0;
            digestMsg.msgType = MsgType::Digest;
            memcpy(digestMsg.payload, digestPayload, dLen);
            digestMsg.payloadLen = static_cast<uint8_t>(dLen);

            uint8_t buf[MESSAGE_HEADER_SIZE + sizeof(digestPayload)];
            size_t encLen = digestMsg.encode(buf, sizeof(buf));
            if (encLen > 0) {
                _lora.send(buf, encLen);
                Serial.printf("[Relay] Sent digest (%d senders)\n", digestPayload[0]);
            }
        }

        // Start sync session with this peer
        _sync.active = true;
        _sync.peerId = msg.senderId;
        _sync.startMs = millis();
        _sync.lastActivityMs = millis();
        _sync.digestSent = true;
        _sync.theyNeed.count = 0;
        _sync.sendIndex = 0;
    }
}

void Relay::handleDigest(const Message& msg) {
    Serial.printf("[Relay] Digest from %d (%d bytes)\n", msg.senderId, msg.payloadLen);

    // Reject digest from third party if we already have an active session
    if (_sync.active && msg.senderId != _sync.peerId) return;

    // Decode their digest, compute what we need
    SyncNeeds weNeed;
    _store.decodeDigest(msg.payload, msg.payloadLen, weNeed);

    if (weNeed.count > 0) {
        // Send SyncRequest for what we need
        uint8_t reqPayload[1 + MAX_SENDERS * 5];
        size_t rLen = MessageStore::encodeSyncRequest(weNeed, reqPayload, sizeof(reqPayload));
        if (rLen > 0) {
            Message reqMsg;
            reqMsg.senderId = DEVICE_ID;
            reqMsg.seq = 0;
            reqMsg.timestamp = 0;
            reqMsg.msgType = MsgType::SyncRequest;
            memcpy(reqMsg.payload, reqPayload, rLen);
            reqMsg.payloadLen = static_cast<uint8_t>(rLen);

            uint8_t buf[MESSAGE_HEADER_SIZE + sizeof(reqPayload)];
            size_t encLen = reqMsg.encode(buf, sizeof(buf));
            if (encLen > 0) {
                _lora.send(buf, encLen);
                Serial.printf("[Relay] Sent SyncRequest for %d senders\n", weNeed.count);
            }
        }
    }

    uint32_t now = millis();

    // If we don't have an active sync session, send our own digest back
    if (!_sync.active) {
        _sync.active = true;
        _sync.peerId = msg.senderId;
        _sync.startMs = now;
        _sync.lastActivityMs = now;
        _sync.theyNeed.count = 0;
        _sync.sendIndex = 0;

        if (!_sync.digestSent) {
            uint8_t digestPayload[1 + MAX_SENDERS * 5];
            size_t dLen = _store.encodeDigest(digestPayload, sizeof(digestPayload), now);
            if (dLen > 0) {
                Message digestMsg;
                digestMsg.senderId = DEVICE_ID;
                digestMsg.seq = 0;
                digestMsg.timestamp = 0;
                digestMsg.msgType = MsgType::Digest;
                memcpy(digestMsg.payload, digestPayload, dLen);
                digestMsg.payloadLen = static_cast<uint8_t>(dLen);

                uint8_t buf[MESSAGE_HEADER_SIZE + sizeof(digestPayload)];
                size_t encLen = digestMsg.encode(buf, sizeof(buf));
                if (encLen > 0) {
                    _lora.send(buf, encLen);
                }
            }
        }
    }

    _sync.lastActivityMs = millis();
    _sync.digestSent = true;
}

void Relay::handleSyncRequest(const Message& msg) {
    Serial.printf("[Relay] SyncRequest from %d\n", msg.senderId);

    // Reject from third party if we already have an active session
    if (_sync.active && msg.senderId != _sync.peerId) return;

    // Decode what they need from us
    MessageStore::decodeSyncRequest(msg.payload, msg.payloadLen, _sync.theyNeed);

    // Initialize send cursors — start from toSeq (newest first)
    for (uint8_t i = 0; i < _sync.theyNeed.count; i++) {
        _sync.sendSeqCursor[i] = _sync.theyNeed.entries[i].toSeq;
    }
    _sync.sendIndex = 0;
    _sync.lastActivityMs = millis();

    Serial.printf("[Relay] Will send %d ranges, newest first\n", _sync.theyNeed.count);
}

void Relay::continueSyncSend() {
    if (_sync.theyNeed.count == 0) {
        _sync.active = false;
        return;
    }

    // Round-robin across senders, newest first
    uint8_t tried = 0;
    while (tried < _sync.theyNeed.count) {
        uint8_t idx = _sync.sendIndex % _sync.theyNeed.count;
        SyncNeed& need = _sync.theyNeed.entries[idx];
        uint16_t cursor = _sync.sendSeqCursor[idx];

        if (cursor >= need.fromSeq) {
            // Find this message in our store
            const SenderLog* sender = _store.getSender(need.senderId);
            if (sender) {
                const MessageEntry* entry = sender->getMessage(cursor);
                if (entry) {
                    // Send it
                    Message msg = Message::fromEntry(*entry);
                    uint8_t buf[MAX_MSG_PAYLOAD + MESSAGE_HEADER_SIZE];
                    size_t len = msg.encode(buf, sizeof(buf));
                    if (len > 0) {
                        _lora.send(buf, len);
                        _sync.lastActivityMs = millis();
                    }
                }
            }
            _sync.sendSeqCursor[idx] = cursor - 1;
            _sync.sendIndex++;
            return; // One message per process() call
        }

        // This sender is done
        _sync.sendIndex++;
        tried++;
    }

    // All senders done
    Serial.println("[Relay] Sync send complete");
    resetSyncSession();
}

void Relay::resetSyncSession() {
    _sync = SyncSession{};
}

// --- Beacon ---

void Relay::sendBeacon() {
    uint16_t hash = _store.stateHash(millis());
    Message beacon = Message::createBeacon(DEVICE_ID, hash, NODE_TYPE);

    uint8_t buf[MESSAGE_HEADER_SIZE + 3];
    size_t len = beacon.encode(buf, sizeof(buf));
    if (len > 0) {
        _lora.send(buf, len);
    }
    _lastSentHash = hash;
}

// --- BLE ---

void Relay::pollBle() {
    if (!_bleRxReady) return;
    handleBleCommand(_bleRxBuf, _bleRxLen);
    _bleRxReady = false;
}

void Relay::handleBleCommand(const uint8_t* data, size_t len) {
    if (len < 1) return;
    uint8_t cmd = data[0];

    switch (cmd) {
        case BLE_CMD_SET_CLOCK:
            bleSetClock(data + 1, len - 1);
            break;
        case BLE_CMD_SET_LOCATION:
            bleSetLocation(data + 1, len - 1);
            break;
        case BLE_CMD_SEND_TEXT:
            bleSendText(data + 1, len - 1);
            break;
        case BLE_CMD_GET_STATE:
            bleGetState();
            break;
        case BLE_CMD_GET_MESSAGES:
            bleGetMessages(data + 1, len - 1);
            break;
        case BLE_CMD_GET_SELF_INFO:
            bleGetSelfInfo();
            break;
        default:
            Serial.printf("[Relay] Unknown BLE cmd: 0x%02X\n", cmd);
            break;
    }
}

void Relay::bleSetClock(const uint8_t* data, size_t len) {
    if (len < 4) return;
    uint32_t unixTime = (static_cast<uint32_t>(data[0]) << 24) |
                        (static_cast<uint32_t>(data[1]) << 16) |
                        (static_cast<uint32_t>(data[2]) << 8) |
                        static_cast<uint32_t>(data[3]);
    _clockOffset = static_cast<int32_t>(unixTime) - static_cast<int32_t>(millis() / 1000);
    Serial.printf("[Relay] Clock set: unix=%lu offset=%ld\n",
        (unsigned long)unixTime, (long)_clockOffset);
}

void Relay::bleSetLocation(const uint8_t* data, size_t len) {
    if (len < 2) return;
    uint8_t nodeA = data[0];
    uint8_t nodeB = data[1];

    // Create location message from ourselves
    uint32_t ts = currentTimestamp();
    Message msg = Message::createLocation(DEVICE_ID, _nextSeq++, ts, nodeA, nodeB);
    saveSeq();

    // Store locally
    MessageEntry entry = msg.toEntry();
    _store.storeMessage(entry, millis());

    // Broadcast on LoRa
    uint8_t buf[MESSAGE_HEADER_SIZE + 2];
    size_t encLen = msg.encode(buf, sizeof(buf));
    if (encLen > 0) {
        _lora.send(buf, encLen);
    }

    Serial.printf("[Relay] Location set: %d-%d\n", nodeA, nodeB);
}

void Relay::bleSendText(const uint8_t* data, size_t len) {
    if (len < 2) return;
    uint8_t textLen = data[0];
    if (len < 1 + textLen) return;

    uint32_t ts = currentTimestamp();
    Message msg = Message::createText(DEVICE_ID, _nextSeq++, ts, data + 1, textLen);
    saveSeq();

    // Store locally
    MessageEntry entry = msg.toEntry();
    _store.storeMessage(entry, millis());

    // Broadcast on LoRa
    uint8_t buf[MAX_MSG_PAYLOAD + MESSAGE_HEADER_SIZE];
    size_t encLen = msg.encode(buf, sizeof(buf));
    if (encLen > 0) {
        _lora.send(buf, encLen);
    }

    Serial.printf("[Relay] Text sent: %d bytes\n", textLen);
}

void Relay::bleGetState() {
    // Send state response: [0x80][count][per sender: senderId, highSeq, locSeq, nodeA, nodeB, presence]
    uint8_t buf[1 + 1 + MAX_SENDERS * 8]; // cmd + count + entries
    buf[0] = BLE_RESP_STATE;
    uint8_t count = 0;
    size_t pos = 2;

    uint32_t now = millis();
    _store.forEachSender([&](const SenderLog& s) {
        if (pos + 8 > sizeof(buf)) return;
        buf[pos++] = s.senderId;
        buf[pos++] = static_cast<uint8_t>(s.highSeq >> 8);
        buf[pos++] = static_cast<uint8_t>(s.highSeq & 0xFF);
        buf[pos++] = static_cast<uint8_t>(s.locSeq >> 8);
        buf[pos++] = static_cast<uint8_t>(s.locSeq & 0xFF);
        buf[pos++] = s.nodeA;
        buf[pos++] = s.nodeB;
        // Presence
        uint32_t elapsed = now - s.lastHeardMs;
        if (elapsed < PRESENCE_STALE_MS) buf[pos++] = 0; // online
        else if (elapsed < PRESENCE_TIMEOUT_MS) buf[pos++] = 1; // stale
        else buf[pos++] = 2; // offline
        count++;
    });
    buf[1] = count;

    _ble.send(buf, pos);
}

void Relay::bleGetMessages(const uint8_t* data, size_t len) {
    if (len < 3) return;
    uint8_t senderId = data[0];
    uint16_t fromSeq = (static_cast<uint16_t>(data[1]) << 8) | data[2];

    const SenderLog* sender = _store.getSender(senderId);
    if (!sender) return;

    // Set up chunked streaming — one message per loop iteration in process()
    _bleGetMsg.active = true;
    _bleGetMsg.senderId = senderId;
    _bleGetMsg.currentSeq = fromSeq;
    _bleGetMsg.endSeq = sender->highSeq;
}

void Relay::bleGetSelfInfo() {
    // [resp][deviceId][clock:4][activeSenders][bootCount:4]
    uint8_t buf[12];
    buf[0] = BLE_RESP_SELF_INFO;
    buf[1] = DEVICE_ID;
    uint32_t ts = currentTimestamp();
    buf[2] = static_cast<uint8_t>(ts >> 24);
    buf[3] = static_cast<uint8_t>(ts >> 16);
    buf[4] = static_cast<uint8_t>(ts >> 8);
    buf[5] = static_cast<uint8_t>(ts & 0xFF);
    buf[6] = _store.activeSenderCount();
    buf[7] = static_cast<uint8_t>(_bootCount >> 24);
    buf[8] = static_cast<uint8_t>(_bootCount >> 16);
    buf[9] = static_cast<uint8_t>(_bootCount >> 8);
    buf[10] = static_cast<uint8_t>(_bootCount & 0xFF);
    _ble.send(buf, 11);
}

void Relay::blePushMessage(const MessageEntry& entry) {
    // [0x82][senderId:1][seq:2][ts:4][type:1][payload:N]
    uint8_t buf[1 + 8 + MAX_MSG_PAYLOAD];
    buf[0] = BLE_NOTIFY_INCOMING;
    buf[1] = entry.senderId;
    buf[2] = static_cast<uint8_t>(entry.seq >> 8);
    buf[3] = static_cast<uint8_t>(entry.seq & 0xFF);
    buf[4] = static_cast<uint8_t>(entry.timestamp >> 24);
    buf[5] = static_cast<uint8_t>(entry.timestamp >> 16);
    buf[6] = static_cast<uint8_t>(entry.timestamp >> 8);
    buf[7] = static_cast<uint8_t>(entry.timestamp & 0xFF);
    buf[8] = static_cast<uint8_t>(entry.msgType);
    if (entry.payloadLen > 0) {
        memcpy(buf + 9, entry.payload, entry.payloadLen);
    }
    _ble.send(buf, 9 + entry.payloadLen);
}

// --- Presence ---

void Relay::checkPresence() {
    uint32_t now = millis();

    _store.forEachSender([&](const SenderLog& s) {
        // Find or create presence state
        PresenceState* ps = nullptr;
        for (auto& p : _presence) {
            if (p.tracked && p.senderId == s.senderId) {
                ps = &p;
                break;
            }
        }
        if (!ps) {
            for (auto& p : _presence) {
                if (!p.tracked) {
                    p.tracked = true;
                    p.senderId = s.senderId;
                    p.last = Presence::Offline;
                    ps = &p;
                    break;
                }
            }
        }
        if (!ps) return;

        // Compute current presence
        uint32_t elapsed = now - s.lastHeardMs;
        Presence current;
        if (elapsed < PRESENCE_STALE_MS) current = Presence::Online;
        else if (elapsed < PRESENCE_TIMEOUT_MS) current = Presence::Stale;
        else current = Presence::Offline;

        // Notify on change
        if (current != ps->last) {
            ps->last = current;
            if (_ble.isConnected()) {
                uint8_t buf[3] = {
                    BLE_NOTIFY_PRESENCE,
                    s.senderId,
                    static_cast<uint8_t>(current)
                };
                _ble.send(buf, 3);
            }
        }
    });
}

#endif
