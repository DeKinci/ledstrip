#ifndef MICROPROTO_MESSAGES_SCHEMA_H
#define MICROPROTO_MESSAGES_SCHEMA_H

#include "../wire/Buffer.h"
#include "../wire/OpCode.h"
#include "../wire/TypeCodec.h"
#include "../PropertyBase.h"

namespace MicroProto {

enum class SchemaItemType : uint8_t {
    NAMESPACE = 0,
    PROPERTY = 1,
    FUNCTION = 2
};

/**
 * SchemaEncoder - Encode SCHEMA_UPSERT messages from PropertyBase list
 */
class SchemaEncoder {
public:
    static bool encodeProperty(WriteBuffer& buf, const PropertyBase* prop);
    static size_t encodeAllProperties(WriteBuffer& buf);

private:
    static bool encodePropertyItem(WriteBuffer& buf, const PropertyBase* prop);
};

/**
 * SchemaDeleteEncoder - Encode SCHEMA_DELETE messages
 *
 * Wire format per spec section 4.5:
 *   u8 operation_header { opcode: 0x4, flags }
 *   [u8 batch_count]     // If batch=1 (count-1)
 *   // For each deletion:
 *   u8 item_type_flags { type: bit4, reserved: bit4 }
 *   propid item_id
 */
class SchemaDeleteEncoder {
public:
    /**
     * Encode single property deletion
     */
    static bool encodePropertyDelete(WriteBuffer& buf, uint16_t propertyId);

    /**
     * Encode batched property deletions
     * @param propertyIds Array of property IDs to delete
     * @param count Number of properties (1-256)
     */
    static bool encodeBatchedDelete(WriteBuffer& buf, const uint16_t* propertyIds, size_t count);
};

/**
 * SchemaDeleteDecoder - Decode SCHEMA_DELETE messages
 */
class SchemaDeleteDecoder {
public:
    struct DeleteItem {
        SchemaItemType type;
        uint16_t itemId;
    };

    /**
     * Decode a SCHEMA_DELETE message
     * @param buf ReadBuffer positioned after opcode byte
     * @param flags Flags from opcode header
     * @param items Output array for delete items
     * @param maxItems Maximum items to decode
     * @param itemCount Output: actual number of items decoded
     * @return true on success
     */
    static bool decode(ReadBuffer& buf, uint8_t flags, DeleteItem* items,
                       size_t maxItems, size_t& itemCount);
};

/**
 * PropertyEncoder - Encode all property values as batched PROPERTY_UPDATE
 */
class PropertyEncoder {
public:
    static size_t encodeAllValues(WriteBuffer& buf);
};

} // namespace MicroProto

#endif // MICROPROTO_MESSAGES_SCHEMA_H