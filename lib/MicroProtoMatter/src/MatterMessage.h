#pragma once
#include <stdint.h>
#include <stddef.h>
#include "MatterConst.h"

namespace matter {

// ---------------------------------------------------------------------------
// MessageHeader – unencrypted outer header of every Matter message
// ---------------------------------------------------------------------------
struct MessageHeader {
    uint8_t  flags         = 0;
    uint8_t  securityFlags = 0;
    uint16_t sessionId     = 0;
    uint32_t messageCounter= 0;
    uint64_t sourceNodeId  = 0;
    uint64_t destNodeId    = 0;
    bool     hasSrc        = false;
    bool     hasDst        = false;

    size_t encode(uint8_t* buf, size_t cap) const {
        size_t needed = 8 + (hasSrc ? 8 : 0) + (hasDst ? 8 : 0);
        if (cap < needed) return 0;
        size_t p = 0;
        uint8_t f = flags & ~(kMsgFlagSBit | kMsgFlagDSIZ_Mask);
        if (hasSrc) f |= kMsgFlagSBit;
        if (hasDst) f |= kMsgFlagDSIZ_Node;
        buf[p++] = f;
        buf[p++] = securityFlags;
        buf[p++] = sessionId & 0xFF; buf[p++] = sessionId >> 8;
        buf[p++] = messageCounter & 0xFF; buf[p++] = (messageCounter >> 8) & 0xFF;
        buf[p++] = (messageCounter >> 16) & 0xFF; buf[p++] = (messageCounter >> 24) & 0xFF;
        if (hasSrc) { for (int i = 0; i < 8; i++) buf[p++] = (sourceNodeId >> (i*8)) & 0xFF; }
        if (hasDst) { for (int i = 0; i < 8; i++) buf[p++] = (destNodeId >> (i*8)) & 0xFF; }
        return p;
    }

    size_t decode(const uint8_t* buf, size_t len) {
        if (len < 8) return 0;
        size_t p = 0;
        flags = buf[p++];
        securityFlags = buf[p++];
        sessionId = buf[p] | ((uint16_t)buf[p+1] << 8); p += 2;
        messageCounter = buf[p] | ((uint32_t)buf[p+1] << 8) |
                         ((uint32_t)buf[p+2] << 16) | ((uint32_t)buf[p+3] << 24); p += 4;
        hasSrc = (flags & kMsgFlagSBit) != 0;
        uint8_t dsiz = flags & kMsgFlagDSIZ_Mask;
        hasDst = (dsiz == kMsgFlagDSIZ_Node);
        if (hasSrc) {
            if (p + 8 > len) return 0;
            sourceNodeId = 0;
            for (int i = 0; i < 8; i++) sourceNodeId |= ((uint64_t)buf[p++]) << (i*8);
        }
        if (hasDst) {
            if (p + 8 > len) return 0;
            destNodeId = 0;
            for (int i = 0; i < 8; i++) destNodeId |= ((uint64_t)buf[p++]) << (i*8);
        } else if (dsiz == kMsgFlagDSIZ_Group) {
            if (p + 2 > len) return 0;
            p += 2; // Skip 16-bit group ID (not stored)
        }
        return p;
    }

    // Size of the encoded header (for AAD computation)
    size_t encodedSize() const {
        return 8 + (hasSrc ? 8 : 0) + (hasDst ? 8 : 0);
    }
};

// ---------------------------------------------------------------------------
// ProtocolHeader – payload header (encrypted in secured sessions)
// ---------------------------------------------------------------------------
struct ProtocolHeader {
    uint8_t  exchangeFlags = 0;
    uint8_t  opcode        = 0;
    uint16_t exchangeId    = 0;
    uint16_t protocolId    = 0;
    uint32_t ackCounter    = 0;
    bool     isInitiator   = false;
    bool     needsAck      = false;
    bool     hasAck        = false;

    size_t encode(uint8_t* buf, size_t cap) const {
        size_t needed = 6 + (hasAck ? 4 : 0);
        if (cap < needed) return 0;
        size_t p = 0;
        uint8_t ef = 0;
        if (isInitiator) ef |= kExInitiator;
        if (hasAck)      ef |= kExAck;
        if (needsAck)    ef |= kExReliable;
        buf[p++] = ef;
        buf[p++] = opcode;
        buf[p++] = exchangeId & 0xFF; buf[p++] = exchangeId >> 8;
        buf[p++] = protocolId & 0xFF; buf[p++] = protocolId >> 8;
        if (hasAck) {
            buf[p++] = ackCounter & 0xFF; buf[p++] = (ackCounter >> 8) & 0xFF;
            buf[p++] = (ackCounter >> 16) & 0xFF; buf[p++] = (ackCounter >> 24) & 0xFF;
        }
        return p;
    }

    size_t decode(const uint8_t* buf, size_t len) {
        if (len < 6) return 0;
        size_t p = 0;
        exchangeFlags = buf[p++];
        opcode = buf[p++];
        exchangeId = buf[p] | ((uint16_t)buf[p+1] << 8); p += 2;
        protocolId = buf[p] | ((uint16_t)buf[p+1] << 8); p += 2;
        if (exchangeFlags & kExVendor) p += 2; // skip vendor id
        isInitiator = (exchangeFlags & kExInitiator) != 0;
        needsAck    = (exchangeFlags & kExReliable) != 0;
        hasAck      = (exchangeFlags & kExAck) != 0;
        if (hasAck) {
            if (p + 4 > len) return 0;
            ackCounter = buf[p] | ((uint32_t)buf[p+1] << 8) |
                         ((uint32_t)buf[p+2] << 16) | ((uint32_t)buf[p+3] << 24);
            p += 4;
        }
        return p;
    }
};

// ---------------------------------------------------------------------------
// StatusReport – special message format (not TLV)
// ---------------------------------------------------------------------------
struct StatusReport {
    uint16_t generalCode  = 0;
    uint32_t protocolId   = 0;
    uint16_t protocolCode = 0;

    size_t encode(uint8_t* buf, size_t cap) const {
        if (cap < 8) return 0;
        size_t p = 0;
        buf[p++] = generalCode & 0xFF; buf[p++] = generalCode >> 8;
        buf[p++] = protocolId & 0xFF; buf[p++] = (protocolId >> 8) & 0xFF;
        buf[p++] = (protocolId >> 16) & 0xFF; buf[p++] = (protocolId >> 24) & 0xFF;
        buf[p++] = protocolCode & 0xFF; buf[p++] = protocolCode >> 8;
        return p;
    }

    size_t decode(const uint8_t* buf, size_t len) {
        if (len < 8) return 0;
        size_t p = 0;
        generalCode = buf[p] | ((uint16_t)buf[p+1] << 8); p += 2;
        protocolId = buf[p] | ((uint32_t)buf[p+1] << 8) |
                     ((uint32_t)buf[p+2] << 16) | ((uint32_t)buf[p+3] << 24); p += 4;
        protocolCode = buf[p] | ((uint16_t)buf[p+1] << 8); p += 2;
        return p;
    }
};

} // namespace matter
