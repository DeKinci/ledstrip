#ifndef MICROPROTO_WIRE_OPCODE_H
#define MICROPROTO_WIRE_OPCODE_H

#include <stdint.h>

namespace MicroProto {

/**
 * Operation codes from MicroProto protocol spec v1
 */
enum class OpCode : uint8_t {
    HELLO                 = 0x0,
    PROPERTY_UPDATE_SHORT = 0x1,  // Property update with u8 ID
    PROPERTY_UPDATE_LONG  = 0x2,  // Property update with u16 ID
    SCHEMA_UPSERT         = 0x3,  // Create or update schema
    SCHEMA_DELETE         = 0x4,  // Delete schema definition
    RPC_CALL              = 0x5,  // Call remote function
    RPC_RESPONSE          = 0x6,  // Response to RPC call
    ERROR                 = 0x7,  // Error message
    PING                  = 0x8,  // Heartbeat request (client -> server)
    PONG                  = 0x9,  // Heartbeat response (server -> client)
    // 0xA-0xF reserved
};

/**
 * Error codes from protocol spec
 */
enum class ErrorCode : uint16_t {
    SUCCESS                    = 0x0000,
    INVALID_OPCODE             = 0x0001,
    INVALID_PROPERTY_ID        = 0x0002,
    INVALID_FUNCTION_ID        = 0x0003,
    TYPE_MISMATCH              = 0x0004,
    VALIDATION_FAILED          = 0x0005,
    OUT_OF_RANGE               = 0x0006,
    PERMISSION_DENIED          = 0x0007,
    NOT_IMPLEMENTED            = 0x0008,
    PROTOCOL_VERSION_MISMATCH  = 0x0009,
    BUFFER_OVERFLOW            = 0x000A,
    // 0x000B-0xFFFF application-specific
};

/**
 * Protocol constants
 */
constexpr uint8_t PROTOCOL_VERSION = 1;

/**
 * Operation header structure
 *
 * Bit layout:
 *   bits 0-3: opcode (0-15)
 *   bits 4-6: flags (operation-specific)
 *   bit 7: batch_flag (1 if batched)
 */
struct OpHeader {
    uint8_t opcode   : 4;
    uint8_t flags    : 3;
    uint8_t batch    : 1;

    OpHeader() : opcode(0), flags(0), batch(0) {}

    OpHeader(OpCode op, uint8_t f = 0, bool b = false)
        : opcode(static_cast<uint8_t>(op)), flags(f), batch(b ? 1 : 0) {}

    // Encode to single byte
    uint8_t encode() const {
        return (batch << 7) | (flags << 4) | opcode;
    }

    // Decode from single byte
    static OpHeader decode(uint8_t byte) {
        OpHeader h;
        h.opcode = byte & 0x0F;
        h.flags = (byte >> 4) & 0x07;
        h.batch = (byte >> 7) & 0x01;
        return h;
    }

    OpCode getOpCode() const {
        return static_cast<OpCode>(opcode);
    }
};

/**
 * Property update flags
 */
struct PropertyUpdateFlags {
    uint8_t has_timestamp : 1;
    uint8_t force_notify  : 1;
    uint8_t has_version   : 1;
    uint8_t reserved      : 5;

    PropertyUpdateFlags() : has_timestamp(0), force_notify(0), has_version(0), reserved(0) {}

    uint8_t encode() const {
        return (has_version << 2) | (force_notify << 1) | has_timestamp;
    }

    static PropertyUpdateFlags decode(uint8_t byte) {
        PropertyUpdateFlags f;
        f.has_timestamp = byte & 0x01;
        f.force_notify = (byte >> 1) & 0x01;
        f.has_version = (byte >> 2) & 0x01;
        return f;
    }
};

} // namespace MicroProto

#endif // MICROPROTO_WIRE_OPCODE_H