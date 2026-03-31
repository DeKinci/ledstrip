#ifndef MICROPROTO_MESSAGES_HELLO_H
#define MICROPROTO_MESSAGES_HELLO_H

#include "../wire/Buffer.h"
#include "../wire/OpCode.h"

namespace MicroProto {

/**
 * HELLO message - Connection handshake and resynchronization
 *
 * Flags:
 *   bit0: is_response  — 0=request, 1=response
 *   bit1: idle          — 1=register but stay idle (no schema/values/broadcasts)
 *
 * Request (client → server):
 *   u8 operation_header { opcode: 0x0, flags }
 *   u8 protocol_version
 *   varint max_packet_size
 *   varint device_id
 *   u16 schema_version
 *
 * Response (server → client):
 *   u8 operation_header { opcode: 0x0, flags }
 *   u8 protocol_version
 *   varint max_packet_size
 *   varint session_id
 *   varint server_timestamp
 *   u16 schema_version
 *
 * Idle flow (gateway):
 *   Device → Gateway:  HELLO {idle=1} — "register, don't sync"
 *   Gateway → Device:  HELLO {idle=1, is_response=1} — "registered"
 *   ... web client connects ...
 *   Gateway → Device:  HELLO {idle=0} — "activate, send me everything"
 *   Device → Gateway:  HELLO {is_response=1} + SCHEMA + VALUES
 *   ... last web client disconnects ...
 *   Gateway → Device:  HELLO {idle=1} — "go idle, stop broadcasting"
 */

struct Hello {
    bool isResponse = false;
    bool idle = false;
    uint8_t protocolVersion = PROTOCOL_VERSION;
    uint32_t maxPacketSize = 4096;

    // Request fields
    uint32_t deviceId = 0;

    // Response fields
    uint32_t sessionId = 0;
    uint32_t serverTimestamp = 0;

    // Schema version (both request and response, always present)
    uint16_t schemaVersion = 0;

    Hello() = default;

    static Hello request(uint32_t deviceId, uint32_t maxPacketSize = 4096, uint16_t schemaVersion = 0) {
        Hello h;
        h.deviceId = deviceId;
        h.maxPacketSize = maxPacketSize;
        h.schemaVersion = schemaVersion;
        return h;
    }

    static Hello idleRequest(uint32_t deviceId, uint32_t maxPacketSize = 4096) {
        Hello h;
        h.idle = true;
        h.deviceId = deviceId;
        h.maxPacketSize = maxPacketSize;
        return h;
    }

    static Hello response(uint32_t sessionId, uint32_t serverTimestamp, uint32_t maxPacketSize = 4096, uint16_t schemaVersion = 0) {
        Hello h;
        h.isResponse = true;
        h.sessionId = sessionId;
        h.serverTimestamp = serverTimestamp;
        h.maxPacketSize = maxPacketSize;
        h.schemaVersion = schemaVersion;
        return h;
    }

    static Hello idleResponse() {
        Hello h;
        h.isResponse = true;
        h.idle = true;
        return h;
    }

    static Hello activate() {
        Hello h;
        // idle=false, isResponse=false — "activate, send me everything"
        return h;
    }

    static Hello deactivate() {
        Hello h;
        h.idle = true;
        return h;
    }

    bool encode(WriteBuffer& buf) const {
        uint8_t flags = 0;
        if (isResponse) flags |= Flags::IS_RESPONSE;
        if (idle) flags |= 0x02;  // bit1 = idle
        if (!buf.writeByte(encodeOpHeader(OpCode::HELLO, flags))) return false;
        if (!buf.writeByte(protocolVersion)) return false;
        if (buf.writeVarint(maxPacketSize) == 0) return false;

        if (isResponse) {
            if (buf.writeVarint(sessionId) == 0) return false;
            if (buf.writeVarint(serverTimestamp) == 0) return false;
        } else {
            if (buf.writeVarint(deviceId) == 0) return false;
        }

        // Schema version (u16 LE, always present)
        if (!buf.writeByte(schemaVersion & 0xFF)) return false;
        if (!buf.writeByte((schemaVersion >> 8) & 0xFF)) return false;

        return true;
    }

    static bool decode(ReadBuffer& buf, Hello& out) {
        uint8_t headerByte = buf.readByte();
        if (!buf.ok()) return false;

        OpCode opcode;
        uint8_t flags;
        decodeOpHeader(headerByte, opcode, flags);

        if (opcode != OpCode::HELLO) return false;

        out.isResponse = (flags & Flags::IS_RESPONSE) != 0;
        out.idle = (flags & 0x02) != 0;
        out.protocolVersion = buf.readByte();
        out.maxPacketSize = buf.readVarint();

        if (!buf.ok()) return false;

        if (out.isResponse) {
            out.sessionId = buf.readVarint();
            out.serverTimestamp = buf.readVarint();
        } else {
            out.deviceId = buf.readVarint();
        }

        if (!buf.ok()) return false;

        // Schema version (u16 LE)
        uint8_t lo = buf.readByte();
        uint8_t hi = buf.readByte();
        out.schemaVersion = lo | (hi << 8);

        return buf.ok();
    }

    static bool decodePayload(ReadBuffer& buf, uint8_t flags, Hello& out) {
        out.isResponse = (flags & Flags::IS_RESPONSE) != 0;
        out.idle = (flags & 0x02) != 0;
        out.protocolVersion = buf.readByte();
        out.maxPacketSize = buf.readVarint();

        if (!buf.ok()) return false;

        if (out.isResponse) {
            out.sessionId = buf.readVarint();
            out.serverTimestamp = buf.readVarint();
        } else {
            out.deviceId = buf.readVarint();
        }

        if (!buf.ok()) return false;

        uint8_t lo = buf.readByte();
        uint8_t hi = buf.readByte();
        out.schemaVersion = lo | (hi << 8);

        return buf.ok();
    }
};

} // namespace MicroProto

#endif // MICROPROTO_MESSAGES_HELLO_H
