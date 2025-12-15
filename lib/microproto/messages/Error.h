#ifndef MICROPROTO_MESSAGES_ERROR_H
#define MICROPROTO_MESSAGES_ERROR_H

#include "../wire/Buffer.h"
#include "../wire/OpCode.h"

namespace MicroProto {

/**
 * ERROR message - Protocol errors and validation failures
 *
 * Format:
 *   u8 operation_header (opcode=0x7, flags, batch=0)
 *   u16 error_code (little-endian)
 *   varint message_length
 *   bytes message (UTF-8)
 *   [u8 related_opcode] (optional, if flags bit 0 set)
 */

// Error header flags
constexpr uint8_t ERROR_FLAG_HAS_RELATED_OPCODE = 0x01;

struct ErrorMessage {
    ErrorCode code = ErrorCode::SUCCESS;
    const char* message = nullptr;
    size_t messageLen = 0;
    bool hasRelatedOpcode = false;
    uint8_t relatedOpcode = 0;

    ErrorMessage() = default;

    ErrorMessage(ErrorCode c, const char* msg = nullptr)
        : code(c), message(msg), messageLen(msg ? strlen(msg) : 0) {}

    ErrorMessage(ErrorCode c, const char* msg, size_t len)
        : code(c), message(msg), messageLen(len) {}

    /**
     * Encode ERROR message
     */
    bool encode(WriteBuffer& buf) const {
        uint8_t flags = hasRelatedOpcode ? ERROR_FLAG_HAS_RELATED_OPCODE : 0;
        OpHeader header(OpCode::ERROR, flags, false);

        if (!buf.writeByte(header.encode())) return false;
        if (!buf.writeUint16(static_cast<uint16_t>(code))) return false;
        if (!buf.writeVarint(static_cast<uint32_t>(messageLen))) return false;

        if (messageLen > 0 && message) {
            if (!buf.writeBytes(reinterpret_cast<const uint8_t*>(message), messageLen)) {
                return false;
            }
        }

        if (hasRelatedOpcode) {
            if (!buf.writeByte(relatedOpcode)) return false;
        }

        return true;
    }

    /**
     * Decode ERROR message (message pointer points into buffer - zero-copy)
     */
    static bool decode(ReadBuffer& buf, ErrorMessage& out) {
        uint8_t headerByte = buf.readByte();
        if (!buf.ok()) return false;

        OpHeader header = OpHeader::decode(headerByte);
        if (header.getOpCode() != OpCode::ERROR) return false;

        out.code = static_cast<ErrorCode>(buf.readUint16());
        out.messageLen = buf.readVarint();

        if (!buf.ok()) return false;

        // Zero-copy: point directly into buffer
        if (out.messageLen > 0) {
            if (buf.remaining() < out.messageLen) return false;
            // Get pointer to current position in buffer
            out.message = reinterpret_cast<const char*>(buf.data() + buf.position());
            buf.skip(out.messageLen);
        } else {
            out.message = nullptr;
        }

        out.hasRelatedOpcode = (header.flags & ERROR_FLAG_HAS_RELATED_OPCODE) != 0;
        if (out.hasRelatedOpcode) {
            out.relatedOpcode = buf.readByte();
        }

        return buf.ok();
    }

    /**
     * Helper to create common errors
     */
    static ErrorMessage invalidOpcode(uint8_t opcode) {
        ErrorMessage err(ErrorCode::INVALID_OPCODE, "Invalid opcode");
        err.hasRelatedOpcode = true;
        err.relatedOpcode = opcode;
        return err;
    }

    static ErrorMessage invalidPropertyId(uint8_t propId) {
        ErrorMessage err(ErrorCode::INVALID_PROPERTY_ID, "Unknown property ID");
        return err;
    }

    static ErrorMessage typeMismatch() {
        return ErrorMessage(ErrorCode::TYPE_MISMATCH, "Type mismatch");
    }

    static ErrorMessage validationFailed(const char* msg = "Validation failed") {
        return ErrorMessage(ErrorCode::VALIDATION_FAILED, msg);
    }

    static ErrorMessage protocolVersionMismatch() {
        return ErrorMessage(ErrorCode::PROTOCOL_VERSION_MISMATCH, "Protocol version mismatch");
    }

    static ErrorMessage bufferOverflow() {
        return ErrorMessage(ErrorCode::BUFFER_OVERFLOW, "Buffer overflow");
    }
};

} // namespace MicroProto

#endif // MICROPROTO_MESSAGES_ERROR_H