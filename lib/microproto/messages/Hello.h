#ifndef MICROPROTO_MESSAGES_HELLO_H
#define MICROPROTO_MESSAGES_HELLO_H

#include "../wire/Buffer.h"
#include "../wire/OpCode.h"

namespace MicroProto {

/**
 * HELLO message - Connection handshake and resynchronization
 *
 * Client HELLO (request):
 *   u8 operation_header (opcode=0, flags=0, batch=0)
 *   u8 protocol_version
 *   u16 max_packet_size (little-endian)
 *   u32 device_id (little-endian)
 *
 * Server HELLO (response):
 *   u8 operation_header (opcode=0, flags=0, batch=0)
 *   u8 protocol_version
 *   u16 max_packet_size (little-endian)
 *   u32 session_id (little-endian)
 *   u32 server_timestamp (little-endian)
 */

struct HelloRequest {
    uint8_t protocolVersion = PROTOCOL_VERSION;
    uint16_t maxPacketSize = 4096;
    uint32_t deviceId = 0;

    /**
     * Encode client HELLO request
     */
    bool encode(WriteBuffer& buf) const {
        OpHeader header(OpCode::HELLO);
        if (!buf.writeByte(header.encode())) return false;
        if (!buf.writeByte(protocolVersion)) return false;
        if (!buf.writeUint16(maxPacketSize)) return false;
        if (!buf.writeUint32(deviceId)) return false;
        return true;
    }

    /**
     * Decode client HELLO request
     */
    static bool decode(ReadBuffer& buf, HelloRequest& out) {
        uint8_t headerByte = buf.readByte();
        if (!buf.ok()) return false;

        OpHeader header = OpHeader::decode(headerByte);
        if (header.getOpCode() != OpCode::HELLO) return false;

        out.protocolVersion = buf.readByte();
        out.maxPacketSize = buf.readUint16();
        out.deviceId = buf.readUint32();

        return buf.ok();
    }

    /**
     * Size of encoded message
     */
    static constexpr size_t encodedSize() {
        return 1 + 1 + 2 + 4;  // header + version + maxPacket + deviceId
    }
};

struct HelloResponse {
    uint8_t protocolVersion = PROTOCOL_VERSION;
    uint16_t maxPacketSize = 4096;
    uint32_t sessionId = 0;
    uint32_t serverTimestamp = 0;

    /**
     * Encode server HELLO response
     */
    bool encode(WriteBuffer& buf) const {
        OpHeader header(OpCode::HELLO);
        if (!buf.writeByte(header.encode())) return false;
        if (!buf.writeByte(protocolVersion)) return false;
        if (!buf.writeUint16(maxPacketSize)) return false;
        if (!buf.writeUint32(sessionId)) return false;
        if (!buf.writeUint32(serverTimestamp)) return false;
        return true;
    }

    /**
     * Decode server HELLO response
     */
    static bool decode(ReadBuffer& buf, HelloResponse& out) {
        uint8_t headerByte = buf.readByte();
        if (!buf.ok()) return false;

        OpHeader header = OpHeader::decode(headerByte);
        if (header.getOpCode() != OpCode::HELLO) return false;

        out.protocolVersion = buf.readByte();
        out.maxPacketSize = buf.readUint16();
        out.sessionId = buf.readUint32();
        out.serverTimestamp = buf.readUint32();

        return buf.ok();
    }

    /**
     * Size of encoded message
     */
    static constexpr size_t encodedSize() {
        return 1 + 1 + 2 + 4 + 4;  // header + version + maxPacket + sessionId + timestamp
    }
};

} // namespace MicroProto

#endif // MICROPROTO_MESSAGES_HELLO_H