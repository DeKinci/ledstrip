#include "TypeCodec.h"

namespace MicroProto {

bool TypeCodec::encodeProperty(WriteBuffer& buf, const PropertyBase* prop) {
    uint8_t typeId = prop->getTypeId();

    // Handle container types
    if (typeId == TYPE_ARRAY) {
        // ARRAY: Fixed count, no length prefix - just packed elements
        return encodeArray(buf,
                          prop->getElementTypeId(),
                          prop->getData(),
                          prop->getElementCount(),
                          prop->getElementSize());
    }

    if (typeId == TYPE_LIST) {
        // LIST: varint count prefix + packed elements
        return encodeList(buf,
                         prop->getElementTypeId(),
                         prop->getData(),
                         prop->getElementCount(),
                         prop->getElementSize());
    }

    // Basic types: direct encoding
    return encodeBasic(buf, typeId, prop->getData(), prop->getSize());
}

bool TypeCodec::decodeProperty(ReadBuffer& buf, PropertyBase* prop) {
    uint8_t typeId = prop->getTypeId();

    // Handle container types
    if (typeId == TYPE_ARRAY) {
        // ARRAY: Fixed count, no length prefix
        size_t count = prop->getElementCount();
        size_t elementSize = prop->getElementSize();
        size_t totalSize = count * elementSize;

        // Temporary buffer for decoding (max reasonable array size)
        uint8_t temp[256];
        if (totalSize > sizeof(temp)) {
            return false;  // Array too large
        }

        if (!decodeArray(buf, prop->getElementTypeId(), temp, count, elementSize)) {
            return false;
        }

        prop->setData(temp, totalSize);
        return true;
    }

    if (typeId == TYPE_LIST) {
        // LIST: varint count prefix + elements
        size_t maxCount = prop->getMaxElementCount();
        size_t elementSize = prop->getElementSize();

        // Temporary buffer for decoding
        uint8_t temp[512];
        size_t maxBytes = maxCount * elementSize;
        if (maxBytes > sizeof(temp)) {
            maxBytes = sizeof(temp);
            maxCount = maxBytes / elementSize;
        }

        size_t actualCount = 0;
        if (!decodeList(buf, prop->getElementTypeId(), temp, maxCount, elementSize, actualCount)) {
            return false;
        }

        prop->setData(temp, actualCount * elementSize);
        return true;
    }

    // Basic types
    size_t size = prop->getSize();

    // Temporary buffer for decoding basic types
    union {
        bool b;
        int8_t i8;
        uint8_t u8;
        int32_t i32;
        float f32;
        uint8_t bytes[4];
    } temp;

    if (!decodeBasic(buf, typeId, &temp, size)) {
        return false;
    }

    prop->setData(&temp, size);
    return true;
}

} // namespace MicroProto