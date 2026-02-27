#include "TypeCodec.h"
#include "../ObjectProperty.h"
#include "../VariantProperty.h"
#include "../ResourceProperty.h"

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

    if (typeId == TYPE_OBJECT) {
        // OBJECT: Field values in schema order, no field names
        // Just write the raw data buffer (fields are packed contiguously)
        size_t size = prop->getSize();
        const uint8_t* data = static_cast<const uint8_t*>(prop->getData());
        return buf.writeBytes(data, size);
    }

    if (typeId == TYPE_VARIANT) {
        // VARIANT wire format: u8 type_index + value bytes
        // getData() returns just the value data (no type index)
        // getVariantTypeIndex() returns the current discriminant
        uint8_t typeIndex = prop->getVariantTypeIndex();
        if (!buf.writeByte(typeIndex)) return false;

        // Write value data using explicit value size from the variant type definition
        size_t valueSize = prop->getVariantValueSize(typeIndex);
        const uint8_t* data = static_cast<const uint8_t*>(prop->getData());
        return buf.writeBytes(data, valueSize);
    }

    if (typeId == TYPE_RESOURCE) {
        // RESOURCE: varint count + for each resource: (id, version, bodySize, blob headerData)
        // Delegate to the virtual method which ResourceProperty overrides
        return prop->encodeResourceHeaders(buf);
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

        // Temporary buffer for decoding
        uint8_t temp[MICROPROTO_DECODE_BUFFER_SIZE];
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
        uint8_t temp[MICROPROTO_DECODE_LIST_BUFFER_SIZE];
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

    if (typeId == TYPE_OBJECT) {
        // OBJECT: Field values in schema order
        size_t size = prop->getSize();

        // Temporary buffer for decoding
        uint8_t temp[MICROPROTO_DECODE_BUFFER_SIZE];
        if (size > sizeof(temp)) {
            return false;  // Object too large
        }

        // Read raw bytes (fields are packed)
        if (!buf.readBytes(temp, size)) {
            return false;
        }

        prop->setData(temp, size);
        return true;
    }

    if (typeId == TYPE_VARIANT) {
        // VARIANT wire: u8 type_index + value bytes
        uint8_t typeIndex = buf.readUint8();
        if (!buf.ok()) return false;

        size_t typeCount = prop->getElementCount();
        if (typeIndex >= typeCount) return false;

        // Get value size from the type definition at this index
        size_t valueSize = prop->getVariantValueSize(typeIndex);

        // Temporary buffer: [type_index][value]
        uint8_t temp[MICROPROTO_DECODE_BUFFER_SIZE];
        if (1 + valueSize > sizeof(temp)) return false;

        temp[0] = typeIndex;
        if (valueSize > 0 && !buf.readBytes(temp + 1, valueSize)) {
            return false;
        }

        prop->setData(temp, 1 + valueSize);
        return true;
    }

    if (typeId == TYPE_RESOURCE) {
        // Resources are read-only — clients cannot send resource updates via PROPERTY_UPDATE.
        // Reject to prevent silent buffer position corruption in batched messages.
        return false;
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

bool TypeCodec::decodeInto(ReadBuffer& buf, const PropertyBase* prop,
                           uint8_t* outBuf, size_t outBufSize, size_t& decodedSize) {
    uint8_t typeId = prop->getTypeId();

    if (typeId == TYPE_ARRAY) {
        size_t count = prop->getElementCount();
        size_t elementSize = prop->getElementSize();
        size_t totalSize = count * elementSize;
        if (totalSize > outBufSize) return false;

        if (!decodeArray(buf, prop->getElementTypeId(), outBuf, count, elementSize)) {
            return false;
        }
        decodedSize = totalSize;
        return true;
    }

    if (typeId == TYPE_LIST) {
        size_t maxCount = prop->getMaxElementCount();
        size_t elementSize = prop->getElementSize();
        size_t maxBytes = maxCount * elementSize;
        if (maxBytes > outBufSize) {
            maxBytes = outBufSize;
            maxCount = maxBytes / elementSize;
        }

        size_t actualCount = 0;
        if (!decodeList(buf, prop->getElementTypeId(), outBuf, maxCount, elementSize, actualCount)) {
            return false;
        }
        decodedSize = actualCount * elementSize;
        return true;
    }

    if (typeId == TYPE_OBJECT) {
        size_t size = prop->getSize();
        if (size > outBufSize) return false;

        if (!buf.readBytes(outBuf, size)) return false;
        decodedSize = size;
        return true;
    }

    if (typeId == TYPE_VARIANT) {
        uint8_t typeIndex = buf.readUint8();
        if (!buf.ok()) return false;

        size_t typeCount = prop->getElementCount();
        if (typeIndex >= typeCount) return false;

        size_t valueSize = prop->getVariantValueSize(typeIndex);
        if (1 + valueSize > outBufSize) return false;

        outBuf[0] = typeIndex;
        if (valueSize > 0 && !buf.readBytes(outBuf + 1, valueSize)) {
            return false;
        }
        decodedSize = 1 + valueSize;
        return true;
    }

    if (typeId == TYPE_RESOURCE) {
        // Resources are read-only — clients cannot send resource updates via PROPERTY_UPDATE.
        return false;
    }

    // Basic types
    size_t size = prop->getSize();
    if (size > outBufSize) return false;

    if (!decodeBasic(buf, typeId, outBuf, size)) {
        return false;
    }
    decodedSize = size;
    return true;
}

} // namespace MicroProto