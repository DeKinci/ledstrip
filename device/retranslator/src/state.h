#pragma once

#include <array>
#include <cstdint>
#include <cstring>

#ifndef MAX_MSG_PAYLOAD
#define MAX_MSG_PAYLOAD 100
#endif

#ifndef MSGS_PER_SENDER
#define MSGS_PER_SENDER 64
#endif

#ifndef MAX_SENDERS
#define MAX_SENDERS 8
#endif

#ifndef PRESENCE_TIMEOUT_MS
#define PRESENCE_TIMEOUT_MS 600000
#endif

#ifndef PRESENCE_EXPIRE_MS
#define PRESENCE_EXPIRE_MS 3600000
#endif

enum class MsgType : uint8_t {
    Location     = 0x01,
    Text         = 0x02,
    Beacon       = 0x10,
    Digest       = 0x11,
    SyncRequest  = 0x12,
};

enum class Presence : uint8_t {
    Online,   // lastHeardMs < 2 min ago
    Stale,    // lastHeardMs < 10 min ago
    Offline,  // lastHeardMs > 10 min ago
};

struct MessageEntry {
    uint8_t  senderId = 0;
    uint16_t seq = 0;
    uint32_t timestamp = 0;
    MsgType  msgType = MsgType::Location;
    uint8_t  payload[MAX_MSG_PAYLOAD] = {};
    uint8_t  payloadLen = 0;
    bool     valid = false;
};

// Per-sender ring buffer of messages
struct SenderLog {
    uint8_t  senderId = 0;
    uint16_t highSeq = 0;
    uint16_t locSeq = 0;
    uint8_t  nodeA = 0;
    uint8_t  nodeB = 0xFF;
    uint32_t lastHeardMs = 0;
    bool     active = false;

    std::array<MessageEntry, MSGS_PER_SENDER> messages = {};
    uint16_t msgHead = 0;
    uint16_t msgCount = 0;

    // Add a message. Returns true if stored (not duplicate).
    bool addMessage(const MessageEntry& entry);

    // Get message by seq. Returns nullptr if not in buffer.
    const MessageEntry* getMessage(uint16_t seq) const;

    // Lowest seq still in ring buffer.
    uint16_t lowSeq() const;
};

// What a node needs from a peer after comparing digests
struct SyncNeed {
    uint8_t  senderId;
    uint16_t fromSeq;
    uint16_t toSeq;
};

struct SyncNeeds {
    std::array<SyncNeed, MAX_SENDERS> entries = {};
    uint8_t count = 0;

    void add(uint8_t senderId, uint16_t fromSeq, uint16_t toSeq) {
        if (count < MAX_SENDERS) {
            entries[count++] = {senderId, fromSeq, toSeq};
        }
    }
};

class MessageStore {
public:
    // Get or create sender log. Returns nullptr if full.
    SenderLog* getOrCreateSender(uint8_t senderId);

    // Get existing sender log. Returns nullptr if not found.
    SenderLog* getSender(uint8_t senderId);
    const SenderLog* getSender(uint8_t senderId) const;

    // Store a message. Handles dedup by (senderId, seq). Returns true if stored.
    bool storeMessage(const MessageEntry& entry, uint32_t nowMs);

    // Commutative state hash: changes when any new data is merged
    // Skips senders offline > PRESENCE_TIMEOUT_MS
    uint16_t stateHash(uint32_t nowMs) const;

    // Encode digest: [count:1][entries: N * 5 bytes each]
    // Each entry: [senderId:1][highSeq:2][locSeq:2]
    // Skips senders offline > PRESENCE_TIMEOUT_MS
    size_t encodeDigest(uint8_t* buf, size_t maxLen, uint32_t nowMs) const;

    // Update lastHeardMs for an existing sender (does NOT create new entries)
    void updatePresence(uint8_t senderId, uint32_t nowMs);

    // Deactivate senders offline > PRESENCE_EXPIRE_MS
    void purgeExpired(uint32_t nowMs);

    // Decode peer's digest and compute what we need from them
    void decodeDigest(const uint8_t* buf, size_t len, SyncNeeds& needs) const;

    // Encode sync request from SyncNeeds
    static size_t encodeSyncRequest(const SyncNeeds& needs, uint8_t* buf, size_t maxLen);

    // Decode peer's sync request into SyncNeeds (what they need from us)
    static void decodeSyncRequest(const uint8_t* buf, size_t len, SyncNeeds& needs);

    uint8_t activeSenderCount() const;

    template<typename F>
    void forEachSender(F&& callback) const {
        for (uint8_t i = 0; i < _senderCount; i++) {
            if (_senders[i].active) {
                callback(_senders[i]);
            }
        }
    }

    template<typename F>
    void forEachSender(F&& callback) {
        for (uint8_t i = 0; i < _senderCount; i++) {
            if (_senders[i].active) {
                callback(_senders[i]);
            }
        }
    }

private:
    std::array<SenderLog, MAX_SENDERS> _senders = {};
    uint8_t _senderCount = 0;
};
