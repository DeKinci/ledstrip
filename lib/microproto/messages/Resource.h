#ifndef MICROPROTO_MESSAGES_RESOURCE_H
#define MICROPROTO_MESSAGES_RESOURCE_H

#include "../wire/Buffer.h"
#include "../wire/OpCode.h"

namespace MicroProto {

/**
 * ResourceGetEncoder - Encode RESOURCE_GET request/response messages
 *
 * Wire format (section 9.2):
 *   Request:  opcode=0x8, flags=0 + request_id + propid + varint resource_id
 *   Response OK:    opcode=0x8, flags=0b001 + request_id + blob data
 *   Response Error: opcode=0x8, flags=0b011 + request_id + u8 error_code + utf8 message
 */
class ResourceGetEncoder {
public:
    /**
     * Encode RESOURCE_GET request
     */
    static bool encodeRequest(WriteBuffer& buf, uint8_t requestId,
                               uint16_t propertyId, uint32_t resourceId) {
        if (!buf.writeByte(encodeOpHeader(OpCode::RESOURCE_GET, 0))) return false;
        if (!buf.writeByte(requestId)) return false;
        if (!buf.writePropId(propertyId)) return false;
        if (!buf.writeVarint(resourceId)) return false;
        return true;
    }

    /**
     * Encode RESOURCE_GET success response
     */
    static bool encodeResponseOk(WriteBuffer& buf, uint8_t requestId,
                                  const uint8_t* data, size_t dataLen) {
        uint8_t flags = Flags::IS_RESPONSE;  // status=0 (OK)
        if (!buf.writeByte(encodeOpHeader(OpCode::RESOURCE_GET, flags))) return false;
        if (!buf.writeByte(requestId)) return false;
        if (!buf.writeBlob(data, dataLen)) return false;
        return true;
    }

    /**
     * Encode RESOURCE_GET error response
     */
    static bool encodeResponseError(WriteBuffer& buf, uint8_t requestId,
                                     uint8_t errorCode, const char* message = nullptr) {
        uint8_t flags = Flags::IS_RESPONSE | Flags::STATUS_ERROR;
        if (!buf.writeByte(encodeOpHeader(OpCode::RESOURCE_GET, flags))) return false;
        if (!buf.writeByte(requestId)) return false;
        if (!buf.writeByte(errorCode)) return false;
        if (!buf.writeUtf8(message ? message : "")) return false;
        return true;
    }
};

/**
 * ResourcePutEncoder - Encode RESOURCE_PUT request/response messages
 *
 * Wire format (section 9.3):
 *   Request:  opcode=0x9, flags + request_id + propid + varint resource_id
 *             [blob header_value] [blob body_data]
 *   Response OK:    opcode=0x9, flags=0b001 + request_id + varint resource_id
 *   Response Error: opcode=0x9, flags=0b011 + request_id + u8 error_code + utf8 message
 */
class ResourcePutEncoder {
public:
    /**
     * Encode RESOURCE_PUT request
     * @param resourceId 0 = create new, else update existing
     */
    static bool encodeRequest(WriteBuffer& buf, uint8_t requestId,
                               uint16_t propertyId, uint32_t resourceId,
                               const uint8_t* headerData, size_t headerLen,
                               const uint8_t* bodyData, size_t bodyLen) {
        ResourcePutFlags flags;
        flags.isResponse = false;
        flags.updateHeader = (headerData != nullptr && headerLen > 0);
        flags.updateBody = (bodyData != nullptr && bodyLen > 0);

        if (!buf.writeByte(encodeOpHeader(OpCode::RESOURCE_PUT, flags.encode()))) return false;
        if (!buf.writeByte(requestId)) return false;
        if (!buf.writePropId(propertyId)) return false;
        if (!buf.writeVarint(resourceId)) return false;

        if (flags.updateHeader) {
            if (!buf.writeBlob(headerData, headerLen)) return false;
        }
        if (flags.updateBody) {
            if (!buf.writeBlob(bodyData, bodyLen)) return false;
        }
        return true;
    }

    /**
     * Encode RESOURCE_PUT success response
     */
    static bool encodeResponseOk(WriteBuffer& buf, uint8_t requestId, uint32_t resourceId) {
        uint8_t flags = Flags::IS_RESPONSE;  // status=0 (OK)
        if (!buf.writeByte(encodeOpHeader(OpCode::RESOURCE_PUT, flags))) return false;
        if (!buf.writeByte(requestId)) return false;
        if (!buf.writeVarint(resourceId)) return false;
        return true;
    }

    /**
     * Encode RESOURCE_PUT error response
     */
    static bool encodeResponseError(WriteBuffer& buf, uint8_t requestId,
                                     uint8_t errorCode, const char* message = nullptr) {
        uint8_t flags = Flags::IS_RESPONSE | Flags::STATUS_ERROR;
        if (!buf.writeByte(encodeOpHeader(OpCode::RESOURCE_PUT, flags))) return false;
        if (!buf.writeByte(requestId)) return false;
        if (!buf.writeByte(errorCode)) return false;
        if (!buf.writeUtf8(message ? message : "")) return false;
        return true;
    }
};

/**
 * ResourceDeleteEncoder - Encode RESOURCE_DELETE request/response messages
 *
 * Wire format (section 9.4):
 *   Request:  opcode=0xA, flags=0 + request_id + propid + varint resource_id
 *   Response OK:    opcode=0xA, flags=0b001 + request_id
 *   Response Error: opcode=0xA, flags=0b011 + request_id + u8 error_code + utf8 message
 */
class ResourceDeleteEncoder {
public:
    /**
     * Encode RESOURCE_DELETE request
     */
    static bool encodeRequest(WriteBuffer& buf, uint8_t requestId,
                               uint16_t propertyId, uint32_t resourceId) {
        if (!buf.writeByte(encodeOpHeader(OpCode::RESOURCE_DELETE, 0))) return false;
        if (!buf.writeByte(requestId)) return false;
        if (!buf.writePropId(propertyId)) return false;
        if (!buf.writeVarint(resourceId)) return false;
        return true;
    }

    /**
     * Encode RESOURCE_DELETE success response
     */
    static bool encodeResponseOk(WriteBuffer& buf, uint8_t requestId) {
        uint8_t flags = Flags::IS_RESPONSE;  // status=0 (OK)
        if (!buf.writeByte(encodeOpHeader(OpCode::RESOURCE_DELETE, flags))) return false;
        if (!buf.writeByte(requestId)) return false;
        return true;
    }

    /**
     * Encode RESOURCE_DELETE error response
     */
    static bool encodeResponseError(WriteBuffer& buf, uint8_t requestId,
                                     uint8_t errorCode, const char* message = nullptr) {
        uint8_t flags = Flags::IS_RESPONSE | Flags::STATUS_ERROR;
        if (!buf.writeByte(encodeOpHeader(OpCode::RESOURCE_DELETE, flags))) return false;
        if (!buf.writeByte(requestId)) return false;
        if (!buf.writeByte(errorCode)) return false;
        if (!buf.writeUtf8(message ? message : "")) return false;
        return true;
    }
};

/**
 * Resource error codes for RESOURCE_GET/PUT/DELETE
 */
namespace ResourceError {
    constexpr uint8_t NOT_FOUND = 1;
    constexpr uint8_t INVALID_DATA = 2;
    constexpr uint8_t ERROR = 3;
    constexpr uint8_t OUT_OF_SPACE = 4;  // PUT only
}

} // namespace MicroProto

#endif // MICROPROTO_MESSAGES_RESOURCE_H
