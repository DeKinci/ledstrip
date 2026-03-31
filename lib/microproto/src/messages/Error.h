#ifndef MICROPROTO_MESSAGES_ERROR_H
#define MICROPROTO_MESSAGES_ERROR_H

#include "../wire/Buffer.h"
#include "../wire/OpCode.h"
#include <string.h>

namespace MicroProto {

/**
 * ERROR message - Protocol errors and validation failures
 *
 * Flags:
 *   bit0: schema_mismatch  // 1 = state inconsistent, client should resync via HELLO
 *   bit1-3: reserved
 *
 * Format:
 *   u8 operation_header { opcode: 0x7, flags: see above }
 *   u16 error_code (little-endian)
 *   utf8 message (varint length + UTF-8 bytes)
 *   [u8 related_opcode] (optional, application-level)
 *
 * Client behavior:
 *   When schema_mismatch=1, the client's local schema or state is inconsistent
 *   with the server. The client should send HELLO to resynchronize.
 *   When schema_mismatch=0, the error is operational (e.g., validation failed)
 *   and the client can continue normally.
 */

struct ErrorMessage {
    ErrorCode code = ErrorCode::SUCCESS;
    const char* message = nullptr;
    size_t messageLen = 0;
    bool schemaMismatch = false;
    bool hasRelatedOpcode = false;
    uint8_t relatedOpcode = 0;

    ErrorMessage() = default;

    ErrorMessage(ErrorCode c, const char* msg = nullptr, bool mismatch = false)
        : code(c), message(msg), messageLen(msg ? strlen(msg) : 0), schemaMismatch(mismatch) {}

    ErrorMessage(ErrorCode c, const char* msg, size_t len, bool mismatch = false)
        : code(c), message(msg), messageLen(len), schemaMismatch(mismatch) {}

    /**
     * Encode ERROR message
     */
    bool encode(WriteBuffer& buf) const {
        uint8_t flags = schemaMismatch ? Flags::SCHEMA_MISMATCH : 0;
        if (!buf.writeByte(encodeOpHeader(OpCode::ERROR, flags))) return false;
        if (!buf.writeUint16(static_cast<uint16_t>(code))) return false;
        if (!buf.writeUtf8(message)) return false;

        // Note: related_opcode is application-level, not part of spec
        // Encode it if present (after message)
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

        OpCode opcode;
        uint8_t flags;
        decodeOpHeader(headerByte, opcode, flags);

        if (opcode != OpCode::ERROR) return false;

        out.schemaMismatch = (flags & Flags::SCHEMA_MISMATCH) != 0;
        out.code = static_cast<ErrorCode>(buf.readUint16());

        // Read UTF-8 message (zero-copy)
        out.message = buf.readUtf8(out.messageLen);

        return buf.ok();
    }

    /**
     * Decode without consuming header (header already read)
     */
    static bool decodePayload(ReadBuffer& buf, uint8_t flags, ErrorMessage& out) {
        out.schemaMismatch = (flags & Flags::SCHEMA_MISMATCH) != 0;
        out.code = static_cast<ErrorCode>(buf.readUint16());

        // Read UTF-8 message (zero-copy)
        out.message = buf.readUtf8(out.messageLen);

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

    static ErrorMessage invalidPropertyId(uint16_t propId, bool triggerResync = true) {
        return ErrorMessage(ErrorCode::INVALID_PROPERTY_ID, "Unknown property ID", triggerResync);
    }

    static ErrorMessage typeMismatch(bool triggerResync = true) {
        return ErrorMessage(ErrorCode::TYPE_MISMATCH, "Type mismatch", triggerResync);
    }

    static ErrorMessage validationFailed(const char* msg = "Validation failed") {
        return ErrorMessage(ErrorCode::VALIDATION_FAILED, msg, false);
    }

    static ErrorMessage protocolVersionMismatch() {
        return ErrorMessage(ErrorCode::PROTOCOL_VERSION_MISMATCH, "Protocol version mismatch", true);
    }

    static ErrorMessage bufferOverflow() {
        return ErrorMessage(ErrorCode::BUFFER_OVERFLOW, "Buffer overflow", false);
    }

    static ErrorMessage notImplemented(const char* msg = "Not implemented") {
        return ErrorMessage(ErrorCode::NOT_IMPLEMENTED, msg, false);
    }
};

} // namespace MicroProto

#endif // MICROPROTO_MESSAGES_ERROR_H