#ifndef NATIVE_TEST

#include "relay.h"
#include <Arduino.h>
#include <MicroBLE.h>

void Relay::begin() {
    _lastBeacon = millis();
    _lastPresenceCheck = millis();
    _lastStatsPush = millis();
    loadSeq();
    _lastSentHash = _store.stateHash(millis());

    // BLE setup
    MicroBLE::init(BLE_DEVICE_NAME);

    MicroBLE::GattConfig config = {};
    config.serviceUUID = BLE_SERVICE_UUID;
    config.rxUUID = BLE_RX_CHARACTERISTIC;
    config.txUUID = BLE_TX_CHARACTERISTIC;
    config.txIndicate = false;
    config.maxClients = MICROBLE_MAX_CLIENTS;

    _bleService.begin(&_bleHandler, config);

    // Add stats characteristic to the same NUS service
    NimBLEService* svc = MicroBLE::server()->getServiceByUUID(BLE_SERVICE_UUID);
    if (svc) {
        _statsChar = svc->createCharacteristic(
            BLE_STATS_CHARACTERISTIC,
            NIMBLE_PROPERTY::NOTIFY
        );
    }

    MicroBLE::advertising()->addServiceUUID(BLE_SERVICE_UUID);
    MicroBLE::advertising()->enableScanResponse(true);
    MicroBLE::startAdvertising();

    Serial.println("[BLE] Advertising started");
}

void Relay::loadSeq() {
    _prefs.begin("relay", false);
    _nextSeq = _prefs.getUShort("seq", 1);
    if (_nextSeq == 0) _nextSeq = 1;
    _bootCount = _prefs.getULong("boot", 0) + 1;
    _prefs.putULong("boot", _bootCount);
}

void Relay::saveSeq() {
    _prefs.putUShort("seq", _nextSeq);
}

uint16_t Relay::nextSeq() {
    uint16_t seq = _nextSeq++;
    if (_nextSeq == 0) _nextSeq = 1;
    saveSeq();
    return seq;
}

bool Relay::loraSend(const uint8_t* data, size_t len) {
    if (_lora.send(data, len)) {
        _stats.packets_tx++;
        return true;
    }
    _stats.tx_failures++;
    return false;
}

void Relay::process() {
    uint32_t loopStart = micros();
    uint32_t now = millis();

    // 1. Receive from LoRa
    pollLoRa();

    // 2. Process BLE messages
    _bleService.loop();

    // 3. Continue active sync session
    if (_sync.active) {
        if (now - _sync.lastActivityMs > SYNC_TIMEOUT_MS) {
            Serial.println("[Relay] Sync session timed out");
            _stats.sync_timeout++;
            resetSyncSession();
        } else {
            continueSyncSend();
        }
    }

    // 4. Chunked BLE message streaming
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

    // 4.5 Speedtest state machine
    if (_speedtest.phase == SpeedtestPhase::RUNNING) {
        speedtestProcess(now);
    }

    // 5. Beacon timer
    if (now - _lastBeacon >= BEACON_INTERVAL_MS) {
        _lastBeacon = now;
        sendBeacon();
    }

    // 6. Presence + purge (throttled)
    if (now - _lastPresenceCheck >= PRESENCE_CHECK_INTERVAL_MS) {
        _lastPresenceCheck = now;
        checkPresence();
        _store.purgeExpired(now);
    }

    // 7. Push stats (1s interval, only if subscribed)
    if (now - _lastStatsPush >= 1000) {
        _lastStatsPush = now;
        pushStats();
    }

    // Track max loop time
    uint32_t elapsed = micros() - loopStart;
    if (elapsed > _loopMaxUs) _loopMaxUs = elapsed;
}

uint32_t Relay::currentTimestamp() const {
    int32_t t = static_cast<int32_t>(millis() / 1000) + _clockOffset;
    return (t > 0) ? static_cast<uint32_t>(t) : 0;
}

// --- Stats ---

void Relay::pushStats() {
    if (!_statsChar) return;

    // Compute system fields
    _stats.uptime_s = millis() / 1000;
    _stats.free_heap = ESP.getFreeHeap();
    _stats.loop_time_us = _loopMaxUs;
    _loopMaxUs = 0;  // reset after read

    // Serialize 15 x uint32 big-endian
    uint8_t buf[Stats::WIRE_SIZE];
    const uint32_t* fields = reinterpret_cast<const uint32_t*>(&_stats);
    for (size_t i = 0; i < Stats::FIELD_COUNT; i++) {
        uint32_t v = fields[i];
        buf[i * 4 + 0] = static_cast<uint8_t>(v >> 24);
        buf[i * 4 + 1] = static_cast<uint8_t>(v >> 16);
        buf[i * 4 + 2] = static_cast<uint8_t>(v >> 8);
        buf[i * 4 + 3] = static_cast<uint8_t>(v & 0xFF);
    }

    _statsChar->setValue(buf, Stats::WIRE_SIZE);
    _statsChar->notify();
}

void Relay::bleResetStats() {
    _stats = Stats{};
    _loopMaxUs = 0;
    Serial.println("[Relay] Stats reset");
}

// --- BLE helpers ---

bool Relay::bleIsConnected() const {
    return _bleService.connectedCount() > 0;
}

bool Relay::bleSend(const uint8_t* data, size_t len) {
    if (len == 0) return false;
    bool sent = false;
    for (uint8_t i = 0; i < MICROBLE_MAX_CLIENTS; i++) {
        if (_bleService.isConnected(i)) {
            _bleService.sendMessage(i, data, len);
            sent = true;
        }
    }
    return sent;
}

// --- BleHandler ---

void Relay::BleHandler::onMessage(uint8_t slot, const uint8_t* data, size_t len) {
    _relay.handleBleCommand(data, len);
}

void Relay::BleHandler::onConnect(uint8_t slot) {
    Serial.printf("[BLE] Client connected (slot %u)\n", slot);
}

void Relay::BleHandler::onDisconnect(uint8_t slot) {
    Serial.printf("[BLE] Client disconnected (slot %u)\n", slot);
    MicroBLE::startAdvertising();
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
        _stats.decode_failures++;
        Serial.println("[Relay] Failed to decode LoRa message");
        return;
    }

    _stats.packets_rx++;

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
        case MsgType::Ping:
            handlePing(msg);
            break;
        case MsgType::Pong:
            if (_speedtest.phase == SpeedtestPhase::RUNNING) {
                speedtestHandlePong(msg);
            }
            break;
    }
}

void Relay::handleLiveMessage(Message& msg) {
    uint32_t now = millis();
    MessageEntry entry = msg.toEntry();
    bool stored = _store.storeMessage(entry, now);

    if (!stored) {
        _stats.dups_rejected++;
        return;
    }

    _stats.msgs_stored++;
    Serial.printf("[Relay] Live msg sender=%d seq=%d type=%d\n",
        msg.senderId, msg.seq, static_cast<int>(msg.msgType));

    if (bleIsConnected()) {
        blePushMessage(entry);
    }

    _lastSentHash = _store.stateHash(millis());
}

void Relay::handleBeacon(const Message& msg) {
    _stats.beacons_rx++;
    uint32_t now = millis();
    uint16_t peerHash = msg.beaconStateHash();
    uint16_t ourHash = _store.stateHash(now);

    Serial.printf("[Relay] Beacon from %d hash=0x%04X (ours=0x%04X)\n",
        msg.senderId, peerHash, ourHash);

    _peerHash = peerHash;
    _store.updatePresence(msg.senderId, now);

    if (peerHash != ourHash && !_sync.active) {
        _stats.hash_mismatches++;
        sendOurDigest(now);

        _sync.active = true;
        _stats.sync_started++;
        _sync.peerId = msg.senderId;
        _sync.startMs = now;
        _sync.lastActivityMs = now;
        _sync.digestSent = true;
        _sync.theyNeed.count = 0;
        _sync.sendIndex = 0;
    }
}

void Relay::handleDigest(const Message& msg) {
    Serial.printf("[Relay] Digest from %d (%d bytes)\n", msg.senderId, msg.payloadLen);

    if (_sync.active && msg.senderId != _sync.peerId) return;

    SyncNeeds weNeed;
    _store.decodeDigest(msg.payload, msg.payloadLen, weNeed);

    if (weNeed.count > 0) {
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
                loraSend(buf, encLen);
                Serial.printf("[Relay] Sent SyncRequest for %d senders\n", weNeed.count);
            }
        }
    }

    uint32_t now = millis();

    if (!_sync.active) {
        _sync.active = true;
        _stats.sync_started++;
        _sync.peerId = msg.senderId;
        _sync.startMs = now;
        _sync.lastActivityMs = now;
        _sync.theyNeed.count = 0;
        _sync.sendIndex = 0;

        if (!_sync.digestSent) {
            sendOurDigest(now);
        }
    }

    _sync.lastActivityMs = millis();
    _sync.digestSent = true;
}

void Relay::handleSyncRequest(const Message& msg) {
    Serial.printf("[Relay] SyncRequest from %d\n", msg.senderId);

    if (_sync.active && msg.senderId != _sync.peerId) return;

    MessageStore::decodeSyncRequest(msg.payload, msg.payloadLen, _sync.theyNeed);

    for (uint8_t i = 0; i < _sync.theyNeed.count; i++) {
        _sync.sendSeqCursor[i] = _sync.theyNeed.entries[i].toSeq;
    }
    _sync.sendIndex = 0;
    _sync.lastActivityMs = millis();

    Serial.printf("[Relay] Will send %d ranges, newest first\n", _sync.theyNeed.count);
}

void Relay::continueSyncSend() {
    if (_sync.theyNeed.count == 0) {
        _stats.sync_completed++;
        _sync.active = false;
        return;
    }

    uint8_t tried = 0;
    while (tried < _sync.theyNeed.count) {
        uint8_t idx = _sync.sendIndex % _sync.theyNeed.count;
        SyncNeed& need = _sync.theyNeed.entries[idx];
        uint16_t cursor = _sync.sendSeqCursor[idx];

        if (cursor >= need.fromSeq) {
            const SenderLog* sender = _store.getSender(need.senderId);
            if (sender) {
                const MessageEntry* entry = sender->getMessage(cursor);
                if (entry) {
                    Message msg = Message::fromEntry(*entry);
                    uint8_t buf[MAX_MSG_PAYLOAD + MESSAGE_HEADER_SIZE];
                    size_t len = msg.encode(buf, sizeof(buf));
                    if (len > 0) {
                        loraSend(buf, len);
                        _sync.lastActivityMs = millis();
                    }
                }
            }
            _sync.sendSeqCursor[idx] = cursor - 1;
            _sync.sendIndex++;
            return;
        }

        _sync.sendIndex++;
        tried++;
    }

    Serial.println("[Relay] Sync send complete");
    _stats.sync_completed++;
    resetSyncSession();
}

void Relay::resetSyncSession() {
    _sync = SyncSession{};
}

// --- Digest ---

void Relay::sendOurDigest(uint32_t nowMs) {
    uint8_t digestPayload[1 + MAX_SENDERS * 5];
    size_t dLen = _store.encodeDigest(digestPayload, sizeof(digestPayload), nowMs);
    if (dLen == 0) return;

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
        loraSend(buf, encLen);
        Serial.printf("[Relay] Sent digest (%d senders)\n", digestPayload[0]);
    }
}

// --- Beacon ---

void Relay::sendBeacon() {
    uint16_t hash = _store.stateHash(millis());
    Message beacon = Message::createBeacon(DEVICE_ID, hash, NODE_TYPE);

    uint8_t buf[MESSAGE_HEADER_SIZE + 3];
    size_t len = beacon.encode(buf, sizeof(buf));
    if (len > 0) {
        if (loraSend(buf, len)) {
            _stats.beacons_sent++;
        }
    }
    _lastSentHash = hash;
}

// --- BLE commands ---

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
        case BLE_CMD_RESET_STATS:
            bleResetStats();
            break;
        case BLE_CMD_START_SPEEDTEST:
            bleStartSpeedtest(data + 1, len - 1);
            break;
        case BLE_CMD_GET_SPEEDTEST_RESULTS:
            bleGetSpeedtestResults();
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

    uint32_t ts = currentTimestamp();
    Message msg = Message::createLocation(DEVICE_ID, nextSeq(), ts, nodeA, nodeB);

    MessageEntry entry = msg.toEntry();
    _store.storeMessage(entry, millis());
    _stats.msgs_stored++;

    uint8_t buf[MESSAGE_HEADER_SIZE + 2];
    size_t encLen = msg.encode(buf, sizeof(buf));
    if (encLen > 0) {
        loraSend(buf, encLen);
    }

    Serial.printf("[Relay] Location set: %d-%d\n", nodeA, nodeB);
}

void Relay::bleSendText(const uint8_t* data, size_t len) {
    if (len < 2) return;
    uint8_t textLen = data[0];
    if (len < 1 + textLen) return;

    uint32_t ts = currentTimestamp();
    Message msg = Message::createText(DEVICE_ID, nextSeq(), ts, data + 1, textLen);

    MessageEntry entry = msg.toEntry();
    _store.storeMessage(entry, millis());
    _stats.msgs_stored++;

    uint8_t buf[MAX_MSG_PAYLOAD + MESSAGE_HEADER_SIZE];
    size_t encLen = msg.encode(buf, sizeof(buf));
    if (encLen > 0) {
        loraSend(buf, encLen);
    }

    Serial.printf("[Relay] Text sent: %d bytes\n", textLen);
}

void Relay::bleGetState() {
    uint8_t buf[1 + 1 + MAX_SENDERS * 8];
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
        uint32_t elapsed = now - s.lastHeardMs;
        if (elapsed < PRESENCE_STALE_MS) buf[pos++] = 0;
        else if (elapsed < PRESENCE_TIMEOUT_MS) buf[pos++] = 1;
        else buf[pos++] = 2;
        count++;
    });
    buf[1] = count;

    bleSend(buf, pos);
}

void Relay::bleGetMessages(const uint8_t* data, size_t len) {
    if (len < 3) return;
    uint8_t senderId = data[0];
    uint16_t fromSeq = (static_cast<uint16_t>(data[1]) << 8) | data[2];

    const SenderLog* sender = _store.getSender(senderId);
    if (!sender) return;

    _bleGetMsg.active = true;
    _bleGetMsg.senderId = senderId;
    _bleGetMsg.currentSeq = fromSeq;
    _bleGetMsg.endSeq = sender->highSeq;
}

void Relay::bleGetSelfInfo() {
    uint8_t buf[11];
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
    bleSend(buf, 11);
}

void Relay::blePushMessage(const MessageEntry& entry) {
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
    bleSend(buf, 9 + entry.payloadLen);
}

// --- Speedtest ---

void Relay::handlePing(const Message& msg) {
    // Auto-echo: send Pong with same payload
    Message pong;
    pong.senderId = DEVICE_ID;
    pong.seq = 0;
    pong.timestamp = 0;
    pong.msgType = MsgType::Pong;
    pong.payloadLen = msg.payloadLen;
    memcpy(pong.payload, msg.payload, msg.payloadLen);

    uint8_t buf[MESSAGE_HEADER_SIZE + MAX_MSG_PAYLOAD];
    size_t len = pong.encode(buf, sizeof(buf));
    if (len > 0) {
        loraSend(buf, len);
    }
}

void Relay::bleStartSpeedtest(const uint8_t* data, size_t len) {
    if (len < 5) return;
    if (_speedtest.phase == SpeedtestPhase::RUNNING) return;

    _speedtest = SpeedtestState{};
    _speedtest.count = (static_cast<uint16_t>(data[0]) << 8) | data[1];
    _speedtest.intervalMs = (static_cast<uint16_t>(data[2]) << 8) | data[3];
    _speedtest.payloadSize = data[4];

    if (_speedtest.count > MAX_SPEEDTEST_PINGS)
        _speedtest.count = MAX_SPEEDTEST_PINGS;

    uint8_t maxFiller = MAX_MSG_PAYLOAD - 2;
    if (_speedtest.payloadSize > maxFiller)
        _speedtest.payloadSize = maxFiller;

    _speedtest.testStartMs = millis();
    _speedtest.lastPingSentMs = 0;
    _speedtest.phase = SpeedtestPhase::RUNNING;

    Serial.printf("[Speedtest] Started: count=%d interval=%dms payload=%d\n",
        _speedtest.count, _speedtest.intervalMs, _speedtest.payloadSize);
}

void Relay::speedtestProcess(uint32_t nowMs) {
    if (_speedtest.nextPingSeq < _speedtest.count) {
        if (_speedtest.lastPingSentMs == 0 ||
            nowMs - _speedtest.lastPingSentMs >= _speedtest.intervalMs) {
            speedtestSendPing();
            _speedtest.lastPingSentMs = nowMs;
        }
        return;
    }

    // All pings sent — wait for pongs or timeout
    if (_speedtest.lastPingDoneMs == 0) {
        _speedtest.lastPingDoneMs = nowMs;
    }

    bool allReceived = (_speedtest.pongCount >= _speedtest.count);
    bool timedOut = (nowMs - _speedtest.lastPingDoneMs >= SPEEDTEST_PONG_TIMEOUT_MS);

    if (allReceived || timedOut) {
        speedtestComputeResults();
        _speedtest.phase = SpeedtestPhase::DONE;
        Serial.printf("[Speedtest] Done: %d/%d received\n",
            _speedtest.pongCount, _speedtest.count);
    }
}

void Relay::speedtestSendPing() {
    uint16_t pingSeq = _speedtest.nextPingSeq;

    Message ping;
    ping.senderId = DEVICE_ID;
    ping.seq = 0;
    ping.timestamp = 0;
    ping.msgType = MsgType::Ping;
    ping.payload[0] = static_cast<uint8_t>(pingSeq >> 8);
    ping.payload[1] = static_cast<uint8_t>(pingSeq & 0xFF);
    memset(ping.payload + 2, 0xAA, _speedtest.payloadSize);
    ping.payloadLen = 2 + _speedtest.payloadSize;

    uint8_t buf[MESSAGE_HEADER_SIZE + MAX_MSG_PAYLOAD];
    size_t len = ping.encode(buf, sizeof(buf));
    if (len > 0) {
        _speedtest.sendTimeUs[pingSeq] = micros();
        loraSend(buf, len);
    }

    _speedtest.nextPingSeq++;
}

void Relay::speedtestHandlePong(const Message& msg) {
    if (msg.payloadLen < 2) return;
    uint16_t pingSeq = (static_cast<uint16_t>(msg.payload[0]) << 8) | msg.payload[1];
    if (pingSeq >= _speedtest.count) return;
    if (_speedtest.sendTimeUs[pingSeq] == 0) return;  // dup or unexpected

    uint32_t rttUs = micros() - _speedtest.sendTimeUs[pingSeq];
    uint16_t rttMs = static_cast<uint16_t>(rttUs / 1000);
    if (rttMs == 0) rttMs = 1;

    _speedtest.sendTimeUs[pingSeq] = 0;
    _speedtest.rttMs[_speedtest.rttCount++] = rttMs;
    _speedtest.pongCount++;
}

void Relay::speedtestComputeResults() {
    auto& r = _speedtest.results;
    r.count = _speedtest.count;
    r.intervalMs = _speedtest.intervalMs;
    r.payloadSize = _speedtest.payloadSize;
    r.totalSent = _speedtest.nextPingSeq;
    r.totalReceived = _speedtest.pongCount;
    r.totalLost = r.totalSent - r.totalReceived;
    r.lossRatePctX10 = (r.totalSent > 0)
        ? static_cast<uint16_t>((static_cast<uint32_t>(r.totalLost) * 1000) / r.totalSent)
        : 0;
    r.testDurationMs = millis() - _speedtest.testStartMs;
    r.actualIntervalAvgMs = (r.totalSent > 1)
        ? static_cast<uint16_t>(r.testDurationMs / (r.totalSent - 1))
        : 0;

    uint16_t n = _speedtest.rttCount;
    if (n == 0) {
        r.rttMin = r.rttMax = r.rttAvg = 0;
        r.rttP1 = r.rttP5 = r.rttP10 = r.rttP25 = 0;
        r.rttP50 = r.rttP75 = r.rttP90 = r.rttP95 = r.rttP99 = 0;
        return;
    }

    // Insertion sort (n <= 256)
    uint16_t* arr = _speedtest.rttMs;
    for (uint16_t i = 1; i < n; i++) {
        uint16_t key = arr[i];
        int16_t j = i - 1;
        while (j >= 0 && arr[j] > key) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
    }

    r.rttMin = arr[0];
    r.rttMax = arr[n - 1];
    uint32_t sum = 0;
    for (uint16_t i = 0; i < n; i++) sum += arr[i];
    r.rttAvg = static_cast<uint16_t>(sum / n);

    auto pctIdx = [n](uint8_t p) -> uint16_t {
        return static_cast<uint16_t>((static_cast<uint32_t>(p) * (n - 1)) / 100);
    };
    r.rttP1  = arr[pctIdx(1)];
    r.rttP5  = arr[pctIdx(5)];
    r.rttP10 = arr[pctIdx(10)];
    r.rttP25 = arr[pctIdx(25)];
    r.rttP50 = arr[pctIdx(50)];
    r.rttP75 = arr[pctIdx(75)];
    r.rttP90 = arr[pctIdx(90)];
    r.rttP95 = arr[pctIdx(95)];
    r.rttP99 = arr[pctIdx(99)];
}

void Relay::bleGetSpeedtestResults() {
    if (_speedtest.phase != SpeedtestPhase::DONE) {
        uint8_t status[2] = {
            BLE_RESP_SPEEDTEST_RESULTS,
            static_cast<uint8_t>(_speedtest.phase == SpeedtestPhase::RUNNING ? 0x01 : 0x00)
        };
        bleSend(status, 2);
        return;
    }

    auto& r = _speedtest.results;
    uint8_t buf[44];
    buf[0] = BLE_RESP_SPEEDTEST_RESULTS;
    buf[1] = static_cast<uint8_t>(r.count >> 8);
    buf[2] = static_cast<uint8_t>(r.count);
    buf[3] = static_cast<uint8_t>(r.intervalMs >> 8);
    buf[4] = static_cast<uint8_t>(r.intervalMs);
    buf[5] = r.payloadSize;
    buf[6] = static_cast<uint8_t>(r.totalSent >> 8);
    buf[7] = static_cast<uint8_t>(r.totalSent);
    buf[8] = static_cast<uint8_t>(r.totalReceived >> 8);
    buf[9] = static_cast<uint8_t>(r.totalReceived);
    buf[10] = static_cast<uint8_t>(r.totalLost >> 8);
    buf[11] = static_cast<uint8_t>(r.totalLost);
    buf[12] = static_cast<uint8_t>(r.lossRatePctX10 >> 8);
    buf[13] = static_cast<uint8_t>(r.lossRatePctX10);

    const uint16_t rttFields[] = {
        r.rttMin, r.rttMax, r.rttAvg,
        r.rttP1, r.rttP5, r.rttP10, r.rttP25,
        r.rttP50, r.rttP75, r.rttP90, r.rttP95, r.rttP99
    };
    size_t pos = 14;
    for (uint16_t v : rttFields) {
        buf[pos++] = static_cast<uint8_t>(v >> 8);
        buf[pos++] = static_cast<uint8_t>(v);
    }

    buf[38] = static_cast<uint8_t>(r.testDurationMs >> 24);
    buf[39] = static_cast<uint8_t>(r.testDurationMs >> 16);
    buf[40] = static_cast<uint8_t>(r.testDurationMs >> 8);
    buf[41] = static_cast<uint8_t>(r.testDurationMs);
    buf[42] = static_cast<uint8_t>(r.actualIntervalAvgMs >> 8);
    buf[43] = static_cast<uint8_t>(r.actualIntervalAvgMs);

    bleSend(buf, 44);
}

// --- Presence ---

void Relay::checkPresence() {
    uint32_t now = millis();

    _store.forEachSender([&](const SenderLog& s) {
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

        uint32_t elapsed = now - s.lastHeardMs;
        Presence current;
        if (elapsed < PRESENCE_STALE_MS) current = Presence::Online;
        else if (elapsed < PRESENCE_TIMEOUT_MS) current = Presence::Stale;
        else current = Presence::Offline;

        if (current != ps->last) {
            ps->last = current;
            if (bleIsConnected()) {
                uint8_t buf[3] = {
                    BLE_NOTIFY_PRESENCE,
                    s.senderId,
                    static_cast<uint8_t>(current)
                };
                bleSend(buf, 3);
            }
        }
    });
}

#endif
