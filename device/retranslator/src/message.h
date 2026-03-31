#pragma once

#include <cstdint>
#include <cstring>
#include "state.h"

#ifndef MAX_MSG_PAYLOAD
#define MAX_MSG_PAYLOAD 100
#endif

// Wire format: [sender_id:1][seq:2][timestamp:4][msg_type:1][payload:N]
static constexpr size_t MESSAGE_HEADER_SIZE = 8;

struct Message {
    uint8_t  senderId = 0;
    uint16_t seq = 0;
    uint32_t timestamp = 0;
    MsgType  msgType = MsgType::Location;
    uint8_t  payload[MAX_MSG_PAYLOAD] = {};
    uint8_t  payloadLen = 0;

    // Encode into buffer. Returns bytes written, 0 on error.
    size_t encode(uint8_t* buf, size_t bufSize) const {
        size_t totalSize = MESSAGE_HEADER_SIZE + payloadLen;
        if (bufSize < totalSize) return 0;

        buf[0] = senderId;
        buf[1] = static_cast<uint8_t>(seq >> 8);
        buf[2] = static_cast<uint8_t>(seq & 0xFF);
        buf[3] = static_cast<uint8_t>(timestamp >> 24);
        buf[4] = static_cast<uint8_t>(timestamp >> 16);
        buf[5] = static_cast<uint8_t>(timestamp >> 8);
        buf[6] = static_cast<uint8_t>(timestamp & 0xFF);
        buf[7] = static_cast<uint8_t>(msgType);
        if (payloadLen > 0) {
            memcpy(buf + MESSAGE_HEADER_SIZE, payload, payloadLen);
        }
        return totalSize;
    }

    // Decode from buffer. Returns true on success.
    bool decode(const uint8_t* buf, size_t len) {
        if (len < MESSAGE_HEADER_SIZE) return false;

        senderId = buf[0];
        seq = (static_cast<uint16_t>(buf[1]) << 8) | buf[2];
        timestamp = (static_cast<uint32_t>(buf[3]) << 24) |
                    (static_cast<uint32_t>(buf[4]) << 16) |
                    (static_cast<uint32_t>(buf[5]) << 8) |
                    static_cast<uint32_t>(buf[6]);
        msgType = static_cast<MsgType>(buf[7]);

        size_t pLen = len - MESSAGE_HEADER_SIZE;
        if (pLen > MAX_MSG_PAYLOAD) return false;

        payloadLen = static_cast<uint8_t>(pLen);
        if (payloadLen > 0) {
            memcpy(payload, buf + MESSAGE_HEADER_SIZE, payloadLen);
        }
        return validatePayload();
    }

    // Factory: location message
    static Message createLocation(uint8_t senderId, uint16_t seq, uint32_t timestamp,
                                   uint8_t nodeA, uint8_t nodeB = 0xFF) {
        Message msg;
        msg.senderId = senderId;
        msg.seq = seq;
        msg.timestamp = timestamp;
        msg.msgType = MsgType::Location;
        msg.payload[0] = nodeA;
        msg.payload[1] = nodeB;
        msg.payloadLen = 2;
        return msg;
    }

    // Factory: text message
    static Message createText(uint8_t senderId, uint16_t seq, uint32_t timestamp,
                               const uint8_t* data, uint8_t len) {
        Message msg;
        msg.senderId = senderId;
        msg.seq = seq;
        msg.timestamp = timestamp;
        msg.msgType = MsgType::Text;
        uint8_t maxText = MAX_MSG_PAYLOAD - 1;
        uint8_t clampedLen = (len > maxText) ? maxText : len;
        msg.payload[0] = clampedLen;
        memcpy(msg.payload + 1, data, clampedLen);
        msg.payloadLen = clampedLen + 1;
        return msg;
    }

    // Factory: beacon
    static Message createBeacon(uint8_t senderId, uint16_t stateHash, uint8_t nodeType) {
        Message msg;
        msg.senderId = senderId;
        msg.seq = 0;
        msg.timestamp = 0;
        msg.msgType = MsgType::Beacon;
        msg.payload[0] = static_cast<uint8_t>(stateHash >> 8);
        msg.payload[1] = static_cast<uint8_t>(stateHash & 0xFF);
        msg.payload[2] = nodeType;
        msg.payloadLen = 3;
        return msg;
    }

    // Location accessors
    uint8_t locationNodeA() const { return payload[0]; }
    uint8_t locationNodeB() const { return payload[1]; }

    // Text accessors
    uint8_t textLength() const { return payload[0]; }
    const uint8_t* textData() const { return payload + 1; }

    // Beacon accessors
    uint16_t beaconStateHash() const {
        return (static_cast<uint16_t>(payload[0]) << 8) | payload[1];
    }
    uint8_t beaconNodeType() const { return payload[2]; }

    // Convert to/from MessageEntry for storage
    MessageEntry toEntry() const {
        MessageEntry e;
        e.senderId = senderId;
        e.seq = seq;
        e.timestamp = timestamp;
        e.msgType = msgType;
        e.payloadLen = payloadLen;
        if (payloadLen > 0) {
            memcpy(e.payload, payload, payloadLen);
        }
        e.valid = true;
        return e;
    }

    static Message fromEntry(const MessageEntry& e) {
        Message msg;
        msg.senderId = e.senderId;
        msg.seq = e.seq;
        msg.timestamp = e.timestamp;
        msg.msgType = e.msgType;
        msg.payloadLen = e.payloadLen;
        if (e.payloadLen > 0) {
            memcpy(msg.payload, e.payload, e.payloadLen);
        }
        return msg;
    }

private:
    bool validatePayload() const {
        switch (msgType) {
            case MsgType::Location:
                return payloadLen == 2;
            case MsgType::Text:
                if (payloadLen < 1) return false;
                return payload[0] == payloadLen - 1;
            case MsgType::Beacon:
                return payloadLen == 3;
            case MsgType::Digest:
                if (payloadLen < 1) return false;
                return payloadLen == 1 + payload[0] * 5; // count + entries
            case MsgType::SyncRequest:
                if (payloadLen < 1) return false;
                return payloadLen == 1 + payload[0] * 5;
            default:
                return false;
        }
    }
};
