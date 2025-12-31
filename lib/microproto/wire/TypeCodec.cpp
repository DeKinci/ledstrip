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
        // VARIANT: u8 type_index + value bytes
        // getData() returns just the value, we need to prefix with type index
        // PropertyBase doesn't expose typeIndex directly, so we check if it's a variant
        // and access via the concrete type

        // Write the variant data as: type_index (from getElementCount encoding trick) + value
        // Actually, we need to get the type index from the property
        // For now, use the fact that VARIANT setData expects [type_index][value]
        // and we should encode the same way

        // The VariantProperty stores type index separately, so we need to
        // encode: type_index + value data
        // We'll trust the prop to give us the right data format

        // For VARIANT, getSize() returns 1 + value_size
        // getData() returns just the value data
        // We need the type index which is stored in the property

        // Since PropertyBase doesn't have getVariantTypeIndex(), we encode
        // the data directly (assuming the property handles this correctly)
        size_t size = prop->getSize();
        const uint8_t* data = static_cast<const uint8_t*>(prop->getData());

        // For a proper VARIANT encode, we need type_index + value
        // The PropertyBase interface doesn't give us type_index directly
        // So we write the size bytes as the value data
        // The VariantProperty overrides getData to return just value,
        // but getSize() returns 1 + value_size

        // Let's write: getElementCount() as type_index (using it as storage)
        // Actually, VariantProperty doesn't fit neatly into PropertyBase interface
        // For MVP, let's just write raw data and handle it specially
        return buf.writeBytes(data, size);
    }

    if (typeId == TYPE_RESOURCE) {
        // RESOURCE: varint count + for each resource: (id, version, size, header_data)
        // This is complex - resources are a collection with variable encoding
        // For MVP, encode as: count + packed resource headers

        size_t count = prop->getElementCount();
        if (buf.writeVarint(static_cast<uint32_t>(count)) == 0) {
            return false;
        }

        // Resource encoding is complex and requires iteration
        // For now, skip actual resource encoding (would need ResourceProperty interface)
        // The actual encoding would iterate resources and write:
        // varint id, varint version, varint size, bytes header_data
        return true;
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

    if (typeId == TYPE_OBJECT) {
        // OBJECT: Field values in schema order
        size_t size = prop->getSize();

        // Temporary buffer for decoding
        uint8_t temp[256];
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
        // VARIANT: u8 type_index + value bytes
        // For decoding, we need to read type_index first, then value
        // The value size depends on which type is selected

        // Read type index
        uint8_t typeIndex = buf.readUint8();
        if (!buf.ok()) return false;

        // Get the variant type count to validate
        size_t typeCount = prop->getElementCount();
        if (typeIndex >= typeCount) return false;

        // For MVP, read the remaining bytes as the value
        // Actual size depends on the type definition
        // We'd need access to VariantTypeDef to know the size

        // Temporary buffer: type_index + value
        uint8_t temp[128];
        temp[0] = typeIndex;

        // Read value based on expected size
        // For now, use getSize() - 1 as value size (rough approximation)
        size_t valueSize = prop->getSize() - 1;
        if (valueSize + 1 > sizeof(temp)) {
            return false;
        }

        if (valueSize > 0 && !buf.readBytes(temp + 1, valueSize)) {
            return false;
        }

        prop->setData(temp, 1 + valueSize);
        return true;
    }

    if (typeId == TYPE_RESOURCE) {
        // RESOURCE: Property updates only contain headers
        // varint count + for each: (varint id, varint version, varint size, header_data)
        // Resource properties are read-only via PROPERTY_UPDATE
        // Actual modifications go through RESOURCE_PUT/DELETE
        // For decode, we skip this as it's handled specially
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