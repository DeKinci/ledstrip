#ifndef MICROPROTO_MESSAGES_HELLO_H
#define MICROPROTO_MESSAGES_HELLO_H

#include "../wire/Buffer.h"
#include "../wire/OpCode.h"

namespace MicroProto {

/**
 * HELLO message - Connection handshake and resynchronization
 *
 * Uses is_response flag (bit0) to distinguish request/response.
 *
 * Request (is_response=0, client -> server):
 *   u8 operation_header { opcode: 0x0, flags: 0x0 }
 *   u8 protocol_version
 *   varint max_packet_size
 *   varint device_id
 *
 * Response (is_response=1, server -> client):
 *   u8 operation_header { opcode: 0x0, flags: 0x1 }
 *   u8 protocol_version
 *   varint max_packet_size
 *   varint session_id
 *   varint server_timestamp
 *
 * Semantics:
 *   Request: "I am (re)connecting. Please send me complete state."
 *   Response: "Reset your state. Complete schema and properties follow."
 */

struct Hello {
    bool isResponse = false;
    uint8_t protocolVersion = PROTOCOL_VERSION;
    uint32_t maxPacketSize = 4096;

    // Request fields (when isResponse=false)
    uint32_t deviceId = 0;

    // Response fields (when isResponse=true)
    uint32_t sessionId = 0;
    uint32_t serverTimestamp = 0;

    Hello() = default;

    /**
     * Create a client request
     */
    static Hello request(uint32_t deviceId, uint32_t maxPacketSize = 4096) {
        Hello h;
        h.isResponse = false;
        h.deviceId = deviceId;
        h.maxPacketSize = maxPacketSize;
        return h;
    }

    /**
     * Create a server response
     */
    static Hello response(uint32_t sessionId, uint32_t serverTimestamp, uint32_t maxPacketSize = 4096) {
        Hello h;
        h.isResponse = true;
        h.sessionId = sessionId;
        h.serverTimestamp = serverTimestamp;
        h.maxPacketSize = maxPacketSize;
        return h;
    }

    /**
     * Encode HELLO message
     */
    bool encode(WriteBuffer& buf) const {
        uint8_t flags = isResponse ? Flags::IS_RESPONSE : 0;
        if (!buf.writeByte(encodeOpHeader(OpCode::HELLO, flags))) return false;
        if (!buf.writeByte(protocolVersion)) return false;
        if (buf.writeVarint(maxPacketSize) == 0) return false;

        if (isResponse) {
            if (buf.writeVarint(sessionId) == 0) return false;
            if (buf.writeVarint(serverTimestamp) == 0) return false;
        } else {
            if (buf.writeVarint(deviceId) == 0) return false;
        }
        return true;
    }

    /**
     * Decode HELLO message
     *
     * Note: Caller should verify protocol version matches and handle mismatch
     */
    static bool decode(ReadBuffer& buf, Hello& out) {
        uint8_t headerByte = buf.readByte();
        if (!buf.ok()) return false;

        OpCode opcode;
        uint8_t flags;
        decodeOpHeader(headerByte, opcode, flags);

        if (opcode != OpCode::HELLO) return false;

        out.isResponse = (flags & Flags::IS_RESPONSE) != 0;
        out.protocolVersion = buf.readByte();
        out.maxPacketSize = buf.readVarint();

        if (!buf.ok()) return false;

        if (out.isResponse) {
            out.sessionId = buf.readVarint();
            out.serverTimestamp = buf.readVarint();
        } else {
            out.deviceId = buf.readVarint();
        }

        return buf.ok();
    }

    /**
     * Decode without consuming header (header already read)
     */
    static bool decodePayload(ReadBuffer& buf, bool isResponse, Hello& out) {
        out.isResponse = isResponse;
        out.protocolVersion = buf.readByte();
        out.maxPacketSize = buf.readVarint();

        if (!buf.ok()) return false;

        if (isResponse) {
            out.sessionId = buf.readVarint();
            out.serverTimestamp = buf.readVarint();
        } else {
            out.deviceId = buf.readVarint();
        }

        return buf.ok();
    }
};

// Backward compatibility aliases
using HelloRequest = Hello;
using HelloResponse = Hello;

} // namespace MicroProto

#endif // MICROPROTO_MESSAGES_HELLO_H