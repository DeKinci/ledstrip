#include "state.h"

// --- SenderLog ---

bool SenderLog::addMessage(const MessageEntry& entry) {
    // Dedup: once highSeq is recorded, any seq <= highSeq is permanently rejected.
    // No need to check ring buffer — monotonic sequences mean <= highSeq is always "already seen."
    if (active && entry.seq <= highSeq) {
        return false;
    }

    // Store in ring buffer
    messages[msgHead] = entry;
    messages[msgHead].valid = true;
    msgHead = (msgHead + 1) % MSGS_PER_SENDER;
    if (msgCount < MSGS_PER_SENDER) msgCount++;

    // Update high water marks
    if (!active || entry.seq > highSeq) {
        highSeq = entry.seq;
    }

    if (entry.msgType == MsgType::Location) {
        locSeq = entry.seq;
        if (entry.payloadLen >= 2) {
            nodeA = entry.payload[0];
            nodeB = entry.payload[1];
        }
    }

    active = true;
    return true;
}

const MessageEntry* SenderLog::getMessage(uint16_t seq) const {
    if (msgCount == 0) return nullptr;
    uint16_t start = (msgCount < MSGS_PER_SENDER)
        ? 0
        : msgHead; // oldest entry when full
    for (uint16_t i = 0; i < msgCount; i++) {
        uint16_t idx = (start + i) % MSGS_PER_SENDER;
        if (messages[idx].valid && messages[idx].seq == seq) {
            return &messages[idx];
        }
    }
    return nullptr;
}

uint16_t SenderLog::lowSeq() const {
    if (msgCount == 0) return 0;
    if (msgCount < MSGS_PER_SENDER) {
        // Buffer not full — oldest is at index 0
        return messages[0].seq;
    }
    // Buffer full — oldest is at msgHead (about to be overwritten)
    return messages[msgHead].seq;
}

// --- MessageStore ---

SenderLog* MessageStore::getOrCreateSender(uint8_t senderId) {
    // Search existing
    for (uint8_t i = 0; i < _senderCount; i++) {
        if (_senders[i].active && _senders[i].senderId == senderId) {
            return &_senders[i];
        }
    }
    // Create new
    if (_senderCount < MAX_SENDERS) {
        SenderLog& s = _senders[_senderCount++];
        s.senderId = senderId;
        s.active = true;
        return &s;
    }
    return nullptr;
}

SenderLog* MessageStore::getSender(uint8_t senderId) {
    for (uint8_t i = 0; i < _senderCount; i++) {
        if (_senders[i].active && _senders[i].senderId == senderId) {
            return &_senders[i];
        }
    }
    return nullptr;
}

const SenderLog* MessageStore::getSender(uint8_t senderId) const {
    for (uint8_t i = 0; i < _senderCount; i++) {
        if (_senders[i].active && _senders[i].senderId == senderId) {
            return &_senders[i];
        }
    }
    return nullptr;
}

bool MessageStore::storeMessage(const MessageEntry& entry, uint32_t nowMs) {
    SenderLog* sender = getOrCreateSender(entry.senderId);
    if (!sender) return false;
    bool stored = sender->addMessage(entry);
    if (stored) {
        sender->lastHeardMs = nowMs;
    }
    return stored;
}

uint16_t MessageStore::stateHash(uint32_t nowMs) const {
    uint16_t hash = 0;
    for (uint8_t i = 0; i < _senderCount; i++) {
        if (!_senders[i].active) continue;
        if (nowMs - _senders[i].lastHeardMs > PRESENCE_TIMEOUT_MS) continue;
        // Commutative per-sender hash: addition is order-independent
        uint16_t h = static_cast<uint16_t>(_senders[i].senderId) * 251 + _senders[i].highSeq;
        h += _senders[i].locSeq * 7;
        hash += h;
    }
    return hash;
}

size_t MessageStore::encodeDigest(uint8_t* buf, size_t maxLen, uint32_t nowMs) const {
    // Count only online senders (within PRESENCE_TIMEOUT_MS)
    uint8_t count = 0;
    for (uint8_t i = 0; i < _senderCount; i++) {
        if (_senders[i].active && (nowMs - _senders[i].lastHeardMs <= PRESENCE_TIMEOUT_MS))
            count++;
    }
    size_t needed = 1 + count * 5;
    if (maxLen < needed) return 0;

    buf[0] = count;
    size_t pos = 1;
    for (uint8_t i = 0; i < _senderCount; i++) {
        if (!_senders[i].active) continue;
        if (nowMs - _senders[i].lastHeardMs > PRESENCE_TIMEOUT_MS) continue;
        buf[pos++] = _senders[i].senderId;
        buf[pos++] = static_cast<uint8_t>(_senders[i].highSeq >> 8);
        buf[pos++] = static_cast<uint8_t>(_senders[i].highSeq & 0xFF);
        buf[pos++] = static_cast<uint8_t>(_senders[i].locSeq >> 8);
        buf[pos++] = static_cast<uint8_t>(_senders[i].locSeq & 0xFF);
    }
    return pos;
}

void MessageStore::decodeDigest(const uint8_t* buf, size_t len, SyncNeeds& needs) const {
    needs.count = 0;
    if (len < 1) return;

    uint8_t count = buf[0];
    if (len < 1 + count * 5) return;

    size_t pos = 1;
    for (uint8_t i = 0; i < count; i++) {
        uint8_t sid = buf[pos++];
        uint16_t peerHighSeq = (static_cast<uint16_t>(buf[pos]) << 8) | buf[pos + 1];
        pos += 2;
        // uint16_t peerLocSeq  = (static_cast<uint16_t>(buf[pos]) << 8) | buf[pos + 1];
        pos += 2; // skip locSeq for now (location is embedded in messages)

        // Compare: if peer has messages we don't have
        const SenderLog* local = getSender(sid);
        uint16_t localHigh = local ? local->highSeq : 0;
        if (peerHighSeq > localHigh) {
            needs.add(sid, localHigh + 1, peerHighSeq);
        }
    }

    // Also check: peer might not know about senders we know about
    // (handled by our own digest being sent to the peer)
}

size_t MessageStore::encodeSyncRequest(const SyncNeeds& needs, uint8_t* buf, size_t maxLen) {
    size_t needed = 1 + needs.count * 5;
    if (maxLen < needed) return 0;

    buf[0] = needs.count;
    size_t pos = 1;
    for (uint8_t i = 0; i < needs.count; i++) {
        buf[pos++] = needs.entries[i].senderId;
        buf[pos++] = static_cast<uint8_t>(needs.entries[i].fromSeq >> 8);
        buf[pos++] = static_cast<uint8_t>(needs.entries[i].fromSeq & 0xFF);
        buf[pos++] = static_cast<uint8_t>(needs.entries[i].toSeq >> 8);
        buf[pos++] = static_cast<uint8_t>(needs.entries[i].toSeq & 0xFF);
    }
    return pos;
}

void MessageStore::decodeSyncRequest(const uint8_t* buf, size_t len, SyncNeeds& needs) {
    needs.count = 0;
    if (len < 1) return;

    uint8_t count = buf[0];
    if (len < 1 + count * 5) return;

    size_t pos = 1;
    for (uint8_t i = 0; i < count; i++) {
        uint8_t sid = buf[pos++];
        uint16_t fromSeq = (static_cast<uint16_t>(buf[pos]) << 8) | buf[pos + 1];
        pos += 2;
        uint16_t toSeq = (static_cast<uint16_t>(buf[pos]) << 8) | buf[pos + 1];
        pos += 2;
        needs.add(sid, fromSeq, toSeq);
    }
}

void MessageStore::updatePresence(uint8_t senderId, uint32_t nowMs) {
    SenderLog* sender = getSender(senderId);
    if (sender) {
        sender->lastHeardMs = nowMs;
    }
}

void MessageStore::purgeExpired(uint32_t nowMs) {
    for (uint8_t i = 0; i < _senderCount; i++) {
        if (_senders[i].active && (nowMs - _senders[i].lastHeardMs > PRESENCE_EXPIRE_MS)) {
            _senders[i].active = false;
        }
    }
}

uint8_t MessageStore::activeSenderCount() const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < _senderCount; i++) {
        if (_senders[i].active) count++;
    }
    return count;
}
