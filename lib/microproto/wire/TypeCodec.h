#ifndef MICROPROTO_WIRE_TYPE_CODEC_H
#define MICROPROTO_WIRE_TYPE_CODEC_H

#include "Buffer.h"
#include "../TypeTraits.h"
#include "../PropertyBase.h"

namespace MicroProto {

// Forward declarations for container properties
class ArrayPropertyBase;
class ListPropertyBase;

/**
 * TypeCodec - Encode/decode values based on type ID
 *
 * Used to serialize property values to wire format and back.
 * Supports basic types and container types (ARRAY, LIST).
 */
class TypeCodec {
public:
    /**
     * Encode a basic type value to the buffer
     *
     * @param buf WriteBuffer to write to
     * @param typeId Type ID (from TypeTraits)
     * @param data Pointer to the value
     * @param size Size of the value in bytes
     * @return true if successful
     */
    static bool encodeBasic(WriteBuffer& buf, uint8_t typeId, const void* data, size_t size) {
        switch (typeId) {
            case TYPE_BOOL:
                if (size != 1) return false;
                return buf.writeBool(*static_cast<const bool*>(data));

            case TYPE_INT8:
                if (size != 1) return false;
                return buf.writeInt8(*static_cast<const int8_t*>(data));

            case TYPE_UINT8:
                if (size != 1) return false;
                return buf.writeUint8(*static_cast<const uint8_t*>(data));

            case TYPE_INT32:
                if (size != 4) return false;
                return buf.writeInt32(*static_cast<const int32_t*>(data));

            case TYPE_FLOAT32:
                if (size != 4) return false;
                return buf.writeFloat32(*static_cast<const float*>(data));

            default:
                return false;  // Unknown type
        }
    }

    /**
     * Encode a value to the buffer based on type ID (legacy alias)
     */
    static bool encode(WriteBuffer& buf, uint8_t typeId, const void* data, size_t size) {
        return encodeBasic(buf, typeId, data, size);
    }

    /**
     * Encode array elements (fixed count, no length prefix)
     *
     * @param buf WriteBuffer to write to
     * @param elementTypeId Element type ID
     * @param data Pointer to element data
     * @param count Number of elements
     * @param elementSize Size of each element
     * @return true if successful
     */
    static bool encodeArray(WriteBuffer& buf, uint8_t elementTypeId, const void* data,
                            size_t count, size_t elementSize) {
        const uint8_t* ptr = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < count; ++i) {
            if (!encodeBasic(buf, elementTypeId, ptr, elementSize)) {
                return false;
            }
            ptr += elementSize;
        }
        return true;
    }

    /**
     * Encode list elements (varint count prefix + elements)
     *
     * @param buf WriteBuffer to write to
     * @param elementTypeId Element type ID
     * @param data Pointer to element data
     * @param count Number of elements
     * @param elementSize Size of each element
     * @return true if successful
     */
    static bool encodeList(WriteBuffer& buf, uint8_t elementTypeId, const void* data,
                           size_t count, size_t elementSize) {
        // Write count as varint
        if (buf.writeVarint(static_cast<uint32_t>(count)) == 0) {
            return false;
        }
        // Write elements
        return encodeArray(buf, elementTypeId, data, count, elementSize);
    }

    /**
     * Encode a property value to the buffer
     * Handles both basic types and container types
     *
     * @param buf WriteBuffer to write to
     * @param prop Property to encode
     * @return true if successful
     */
    static bool encodeProperty(WriteBuffer& buf, const PropertyBase* prop);

    /**
     * Decode a basic type value from the buffer
     *
     * @param buf ReadBuffer to read from
     * @param typeId Type ID (from TypeTraits)
     * @param data Pointer to store the value
     * @param size Expected size of the value
     * @return true if successful
     */
    static bool decodeBasic(ReadBuffer& buf, uint8_t typeId, void* data, size_t size) {
        switch (typeId) {
            case TYPE_BOOL:
                if (size != 1) return false;
                *static_cast<bool*>(data) = buf.readBool();
                return buf.ok();

            case TYPE_INT8:
                if (size != 1) return false;
                *static_cast<int8_t*>(data) = buf.readInt8();
                return buf.ok();

            case TYPE_UINT8:
                if (size != 1) return false;
                *static_cast<uint8_t*>(data) = buf.readUint8();
                return buf.ok();

            case TYPE_INT32:
                if (size != 4) return false;
                *static_cast<int32_t*>(data) = buf.readInt32();
                return buf.ok();

            case TYPE_FLOAT32:
                if (size != 4) return false;
                *static_cast<float*>(data) = buf.readFloat32();
                return buf.ok();

            default:
                return false;  // Unknown type
        }
    }

    /**
     * Decode a value from the buffer based on type ID (legacy alias)
     */
    static bool decode(ReadBuffer& buf, uint8_t typeId, void* data, size_t size) {
        return decodeBasic(buf, typeId, data, size);
    }

    /**
     * Decode array elements (fixed count, no length prefix)
     *
     * @param buf ReadBuffer to read from
     * @param elementTypeId Element type ID
     * @param data Pointer to element storage
     * @param count Number of elements to read
     * @param elementSize Size of each element
     * @return true if successful
     */
    static bool decodeArray(ReadBuffer& buf, uint8_t elementTypeId, void* data,
                            size_t count, size_t elementSize) {
        uint8_t* ptr = static_cast<uint8_t*>(data);
        for (size_t i = 0; i < count; ++i) {
            if (!decodeBasic(buf, elementTypeId, ptr, elementSize)) {
                return false;
            }
            ptr += elementSize;
        }
        return true;
    }

    /**
     * Decode list elements (varint count prefix + elements)
     *
     * @param buf ReadBuffer to read from
     * @param elementTypeId Element type ID
     * @param data Pointer to element storage
     * @param maxCount Maximum elements to read
     * @param elementSize Size of each element
     * @param actualCount Output: actual number of elements read
     * @return true if successful
     */
    static bool decodeList(ReadBuffer& buf, uint8_t elementTypeId, void* data,
                           size_t maxCount, size_t elementSize, size_t& actualCount) {
        uint32_t count = buf.readVarint();
        if (!buf.ok()) return false;

        actualCount = (count < maxCount) ? count : maxCount;

        uint8_t* ptr = static_cast<uint8_t*>(data);
        for (size_t i = 0; i < actualCount; ++i) {
            if (!decodeBasic(buf, elementTypeId, ptr, elementSize)) {
                return false;
            }
            ptr += elementSize;
        }

        // Skip any remaining elements if count > maxCount
        for (size_t i = actualCount; i < count; ++i) {
            if (!buf.skip(elementSize)) {
                return false;
            }
        }

        return true;
    }

    /**
     * Decode a property value from the buffer
     * Handles both basic types and container types
     *
     * @param buf ReadBuffer to read from
     * @param prop Property to decode into
     * @return true if successful
     */
    static bool decodeProperty(ReadBuffer& buf, PropertyBase* prop);

    /**
     * Get the wire size of a type
     *
     * @param typeId Type ID
     * @return Size in bytes, 0 if unknown
     */
    static size_t typeSize(uint8_t typeId) {
        switch (typeId) {
            case TYPE_BOOL:   return 1;
            case TYPE_INT8:   return 1;
            case TYPE_UINT8:  return 1;
            case TYPE_INT32:  return 4;
            case TYPE_FLOAT32: return 4;
            default: return 0;
        }
    }

    /**
     * Get the wire size of a basic type
     */
    static size_t basicTypeSize(uint8_t typeId) {
        return typeSize(typeId);
    }

    /**
     * Get type name for debugging
     */
    static const char* typeName(uint8_t typeId) {
        switch (typeId) {
            case TYPE_BOOL:    return "BOOL";
            case TYPE_INT8:    return "INT8";
            case TYPE_UINT8:   return "UINT8";
            case TYPE_INT32:   return "INT32";
            case TYPE_FLOAT32: return "FLOAT32";
            case TYPE_ARRAY:   return "ARRAY";
            case TYPE_LIST:    return "LIST";
            case TYPE_OBJECT:  return "OBJECT";
            default: return "UNKNOWN";
        }
    }
};

} // namespace MicroProto

#endif // MICROPROTO_WIRE_TYPE_CODEC_H