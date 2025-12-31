#ifndef MICROPROTO_WIRE_OPCODE_H
#define MICROPROTO_WIRE_OPCODE_H

#include <stdint.h>

namespace MicroProto {

/**
 * Operation codes from MicroProto protocol spec v1 (MVP)
 *
 * Message header format:
 *   u8 { opcode: bit4, flags: bit4 }
 *
 * Flags are opcode-specific (see below).
 */
enum class OpCode : uint8_t {
    HELLO           = 0x0,  // Protocol handshake
    PROPERTY_UPDATE = 0x1,  // Property value update (propid encoding)
    // 0x2 reserved for PROPERTY_DELTA (future)
    SCHEMA_UPSERT   = 0x3,  // Create or update schema
    SCHEMA_DELETE   = 0x4,  // Delete schema definition
    RPC             = 0x5,  // Remote procedure call (request + response)
    PING            = 0x6,  // Heartbeat (request + response)
    ERROR           = 0x7,  // Error message
    RESOURCE_GET    = 0x8,  // Get resource body
    RESOURCE_PUT    = 0x9,  // Create/update resource
    RESOURCE_DELETE = 0xA,  // Delete resource
    // 0xB-0xF reserved
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
constexpr uint32_t RPC_TIMEOUT_MS = 60000;  // 60 seconds

/**
 * Encode operation header byte
 *
 * Format: bits 0-3 = opcode, bits 4-7 = flags
 */
inline uint8_t encodeOpHeader(OpCode opcode, uint8_t flags = 0) {
    return static_cast<uint8_t>(opcode) | (flags << 4);
}

/**
 * Decode operation header byte
 */
inline void decodeOpHeader(uint8_t byte, OpCode& opcode, uint8_t& flags) {
    opcode = static_cast<OpCode>(byte & 0x0F);
    flags = (byte >> 4) & 0x0F;
}

/**
 * Opcode-specific flag definitions
 *
 * HELLO (0x0):
 *   bit0: is_response    // 0=request, 1=response
 *
 * PROPERTY_UPDATE (0x1):
 *   bit0: batch          // 1=batched (batch_count follows)
 *   bit1: has_timestamp  // 1=timestamp follows (once for entire batch)
 *
 * SCHEMA_UPSERT (0x3):
 *   bit0: batch          // 1=batched
 *
 * SCHEMA_DELETE (0x4):
 *   bit0: batch          // 1=batched
 *
 * RPC (0x5):
 *   Request (bit0=0):
 *     bit0: is_response     // 0=request
 *     bit1: needs_response  // 1=wait for response, 0=fire-and-forget
 *   Response (bit0=1):
 *     bit0: is_response     // 1=response
 *     bit1: success         // 1=success, 0=error
 *     bit2: has_return_value // 1=value present (only if success=1)
 *
 * PING (0x6):
 *   bit0: is_response    // 0=request, 1=response
 *
 * ERROR (0x7):
 *   bit0: schema_mismatch // 1=state inconsistent, client should resync
 *
 * RESOURCE_GET (0x8):
 *   Request (bit0=0):
 *     bit0: is_response    // 0=request
 *   Response (bit0=1):
 *     bit0: is_response    // 1=response
 *     bit1: status         // 0=ok, 1=error
 *
 * RESOURCE_PUT (0x9):
 *   Request (bit0=0):
 *     bit0: is_response    // 0=request
 *     bit1: update_header  // 1=header_value follows
 *     bit2: update_body    // 1=body_data follows
 *   Response (bit0=1):
 *     bit0: is_response    // 1=response
 *     bit1: status         // 0=ok, 1=error
 *
 * RESOURCE_DELETE (0xA):
 *   bit0: is_response    // 0=request, 1=response
 *   bit1: status         // Response only: 0=ok, 1=error
 */

// Common flag masks
namespace Flags {
    // HELLO, PING, RPC, RESOURCE_*
    constexpr uint8_t IS_RESPONSE = 0x01;

    // PROPERTY_UPDATE, SCHEMA_UPSERT, SCHEMA_DELETE
    constexpr uint8_t BATCH = 0x01;

    // PROPERTY_UPDATE
    constexpr uint8_t HAS_TIMESTAMP = 0x02;

    // RPC request
    constexpr uint8_t NEEDS_RESPONSE = 0x02;

    // RPC response
    constexpr uint8_t SUCCESS = 0x02;
    constexpr uint8_t HAS_RETURN_VALUE = 0x04;

    // ERROR
    constexpr uint8_t SCHEMA_MISMATCH = 0x01;

    // RESOURCE_GET/PUT/DELETE response
    constexpr uint8_t STATUS_ERROR = 0x02;

    // RESOURCE_PUT request
    constexpr uint8_t UPDATE_HEADER = 0x02;
    constexpr uint8_t UPDATE_BODY = 0x04;
}

/**
 * PROPERTY_UPDATE flags helper
 */
struct PropertyUpdateFlags {
    bool batch;
    bool hasTimestamp;

    PropertyUpdateFlags() : batch(false), hasTimestamp(false) {}

    uint8_t encode() const {
        return (batch ? Flags::BATCH : 0) |
               (hasTimestamp ? Flags::HAS_TIMESTAMP : 0);
    }

    static PropertyUpdateFlags decode(uint8_t flags) {
        PropertyUpdateFlags f;
        f.batch = flags & Flags::BATCH;
        f.hasTimestamp = flags & Flags::HAS_TIMESTAMP;
        return f;
    }
};

/**
 * RPC flags helper
 */
struct RpcFlags {
    bool isResponse;
    // Request fields
    bool needsResponse;
    // Response fields
    bool success;
    bool hasReturnValue;

    RpcFlags() : isResponse(false), needsResponse(false), success(false), hasReturnValue(false) {}

    uint8_t encode() const {
        if (isResponse) {
            return Flags::IS_RESPONSE |
                   (success ? Flags::SUCCESS : 0) |
                   (hasReturnValue ? Flags::HAS_RETURN_VALUE : 0);
        } else {
            return (needsResponse ? Flags::NEEDS_RESPONSE : 0);
        }
    }

    static RpcFlags decode(uint8_t flags) {
        RpcFlags f;
        f.isResponse = flags & Flags::IS_RESPONSE;
        if (f.isResponse) {
            f.success = flags & Flags::SUCCESS;
            f.hasReturnValue = flags & Flags::HAS_RETURN_VALUE;
        } else {
            f.needsResponse = flags & Flags::NEEDS_RESPONSE;
        }
        return f;
    }
};

/**
 * RESOURCE_PUT flags helper
 */
struct ResourcePutFlags {
    bool isResponse;
    // Request fields
    bool updateHeader;
    bool updateBody;
    // Response fields
    bool statusError;

    ResourcePutFlags() : isResponse(false), updateHeader(false), updateBody(false), statusError(false) {}

    uint8_t encode() const {
        if (isResponse) {
            return Flags::IS_RESPONSE | (statusError ? Flags::STATUS_ERROR : 0);
        } else {
            return (updateHeader ? Flags::UPDATE_HEADER : 0) |
                   (updateBody ? Flags::UPDATE_BODY : 0);
        }
    }

    static ResourcePutFlags decode(uint8_t flags) {
        ResourcePutFlags f;
        f.isResponse = flags & Flags::IS_RESPONSE;
        if (f.isResponse) {
            f.statusError = flags & Flags::STATUS_ERROR;
        } else {
            f.updateHeader = flags & Flags::UPDATE_HEADER;
            f.updateBody = flags & Flags::UPDATE_BODY;
        }
        return f;
    }
};

} // namespace MicroProto

#endif // MICROPROTO_WIRE_OPCODE_H