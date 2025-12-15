#ifndef MICROPROTO_WIRE_TYPE_CODEC_H
#define MICROPROTO_WIRE_TYPE_CODEC_H

#include "Buffer.h"
#include "../TypeTraits.h"
#include "../PropertyBase.h"

namespace MicroProto {

/**
 * TypeCodec - Encode/decode values based on type ID
 *
 * Used to serialize property values to wire format and back.
 */
class TypeCodec {
public:
    /**
     * Encode a value to the buffer based on type ID
     *
     * @param buf WriteBuffer to write to
     * @param typeId Type ID (from TypeTraits)
     * @param data Pointer to the value
     * @param size Size of the value in bytes
     * @return true if successful
     */
    static bool encode(WriteBuffer& buf, uint8_t typeId, const void* data, size_t size) {
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
     * Encode a property value to the buffer
     *
     * @param buf WriteBuffer to write to
     * @param prop Property to encode
     * @return true if successful
     */
    static bool encodeProperty(WriteBuffer& buf, const PropertyBase* prop) {
        return encode(buf, prop->getTypeId(), prop->getData(), prop->getSize());
    }

    /**
     * Decode a value from the buffer based on type ID
     *
     * @param buf ReadBuffer to read from
     * @param typeId Type ID (from TypeTraits)
     * @param data Pointer to store the value
     * @param size Expected size of the value
     * @return true if successful
     */
    static bool decode(ReadBuffer& buf, uint8_t typeId, void* data, size_t size) {
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
     * Decode a property value from the buffer
     *
     * @param buf ReadBuffer to read from
     * @param prop Property to decode into
     * @return true if successful
     */
    static bool decodeProperty(ReadBuffer& buf, PropertyBase* prop) {
        uint8_t typeId = prop->getTypeId();
        size_t size = prop->getSize();

        // Temporary buffer for decoding
        union {
            bool b;
            int8_t i8;
            uint8_t u8;
            int32_t i32;
            float f32;
            uint8_t bytes[4];
        } temp;

        if (!decode(buf, typeId, &temp, size)) {
            return false;
        }

        prop->setData(&temp, size);
        return true;
    }

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
     * Get type name for debugging
     */
    static const char* typeName(uint8_t typeId) {
        switch (typeId) {
            case TYPE_BOOL:    return "BOOL";
            case TYPE_INT8:    return "INT8";
            case TYPE_UINT8:   return "UINT8";
            case TYPE_INT32:   return "INT32";
            case TYPE_FLOAT32: return "FLOAT32";
            default: return "UNKNOWN";
        }
    }
};

} // namespace MicroProto

#endif // MICROPROTO_WIRE_TYPE_CODEC_H