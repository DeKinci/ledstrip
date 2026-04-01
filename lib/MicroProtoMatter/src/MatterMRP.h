#pragma once
#include <stdint.h>
#include <string.h>
#include "MatterConst.h"

namespace matter {

// ---------------------------------------------------------------------------
// MRP – Message Reliability Protocol
// Handles: message counters, piggybacked ACKs, retransmission, deduplication
// ---------------------------------------------------------------------------
class MRP {
public:
    // --- Counters ---
    uint32_t nextSendCounter() { return _sendCounter++; }

    // Initialize send counter to a random value (call from begin())
    void initCounter(uint32_t randomVal) {
        // Spec: initialize to random in [1, 2^28]
        _sendCounter = (randomVal % 0x0FFFFFFF) + 1;
    }

    // --- Deduplication (32-entry sliding window) ---
    // Returns true if this message counter was already seen (duplicate)
    bool isDuplicate(uint32_t msgCounter) const {
        if (!_recvCounterValid) return false;
        int32_t diff = (int32_t)(msgCounter - _maxRecvCounter);
        if (diff > 0) return false;                         // New (ahead of window)
        if (diff == 0) return true;                         // Exact re-send of max
        uint32_t age = (uint32_t)(-diff);
        if (age > 31) return true;                          // Too old
        return (_recvWindow & (1u << age)) != 0;            // Check bitmap
    }

    // --- ACK tracking ---
    void onReceived(uint32_t msgCounter, bool peerNeedsAck) {
        if (!_recvCounterValid) {
            _maxRecvCounter = msgCounter;
            _recvWindow = 1; // Bit 0 = current counter
            _recvCounterValid = true;
        } else {
            int32_t diff = (int32_t)(msgCounter - _maxRecvCounter);
            if (diff > 0) {
                // New counter ahead — shift window
                if ((uint32_t)diff < 32) {
                    _recvWindow <<= (uint32_t)diff;
                } else {
                    _recvWindow = 0;
                }
                _recvWindow |= 1; // Mark current position
                _maxRecvCounter = msgCounter;
            } else {
                // Counter within window — mark its bit
                uint32_t age = (uint32_t)(-diff);
                if (age < 32) _recvWindow |= (1u << age);
            }
        }
        if (peerNeedsAck) {
            _pendingAck = true;
            _pendingAckCounter = msgCounter;
        }
    }

    void onAckReceived(uint32_t ackedCounter) {
        for (auto& slot : _retransmit) {
            if (slot.active && slot.counter == ackedCounter) {
                slot.active = false;
                return;
            }
        }
    }

    bool hasPendingAck() const { return _pendingAck; }
    uint32_t pendingAckCounter() const { return _pendingAckCounter; }
    void clearPendingAck() { _pendingAck = false; }

    // --- Retransmission (multi-slot with exponential backoff) ---
    void trackSent(const uint8_t* data, size_t len, uint32_t counter) {
        // Find free slot (or evict oldest)
        RetransmitSlot* slot = nullptr;
        for (auto& s : _retransmit) {
            if (!s.active) { slot = &s; break; }
        }
        if (!slot) {
            // Evict the slot with the highest retry count
            slot = &_retransmit[0];
            for (auto& s : _retransmit) {
                if (s.retryCount > slot->retryCount) slot = &s;
            }
        }
        if (len > sizeof(slot->data)) return;
        memcpy(slot->data, data, len);
        slot->len = len;
        slot->counter = counter;
        slot->retryCount = 0;
        slot->nextRetryMs = millis() + kMRPBaseRetryMs;
        slot->active = true;
    }

    // Tick all slots; returns first retransmit needed, nullptr if none
    const uint8_t* tick(size_t& len) {
        uint32_t now = millis();
        for (auto& slot : _retransmit) {
            if (!slot.active) continue;
            if ((int32_t)(now - slot.nextRetryMs) < 0) continue;

            if (slot.retryCount >= kMRPMaxRetries) {
                slot.active = false;
                continue; // Give up
            }
            slot.retryCount++;
            // Backoff: base * 1.6^n, capped at 10s (spec uses 1.6 base factor)
            uint32_t backoff = kMRPBaseRetryMs;
            for (uint8_t i = 0; i < slot.retryCount; i++)
                backoff = backoff * 16 / 10;
            if (backoff > 10000) backoff = 10000;
            slot.nextRetryMs = now + backoff;
            len = slot.len;
            return slot.data;
        }
        return nullptr;
    }

    bool hasActiveRetransmit() const {
        for (const auto& s : _retransmit) {
            if (s.active) return true;
        }
        return false;
    }

    void reset() {
        _pendingAck = false;
        _recvCounterValid = false;
        _recvWindow = 0;
        for (auto& s : _retransmit) s.active = false;
    }

private:
    static uint32_t millis();  // Defined in .cpp (wraps Arduino millis or test mock)

    static constexpr size_t kMaxRetransmitSlots = 2;
    static constexpr uint32_t kMRPBaseRetryMs = 300;

    uint32_t _sendCounter = 1;
    uint32_t _maxRecvCounter = 0;
    uint32_t _recvWindow = 0;       // Bitmap: bit N = counter (max - N) was seen
    bool     _recvCounterValid = false;
    bool     _pendingAck = false;
    uint32_t _pendingAckCounter = 0;

    struct RetransmitSlot {
        uint8_t  data[kMaxMessageSize] = {};
        size_t   len = 0;
        uint32_t counter = 0;
        uint32_t nextRetryMs = 0;
        uint8_t  retryCount = 0;
        bool     active = false;
    } _retransmit[kMaxRetransmitSlots];
};

} // namespace matter
