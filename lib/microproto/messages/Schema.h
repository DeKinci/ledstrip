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
 * PropertyEncoder - Encode all property values as batched PROPERTY_UPDATE
 */
class PropertyEncoder {
public:
    static size_t encodeAllValues(WriteBuffer& buf);
};

} // namespace MicroProto

#endif // MICROPROTO_MESSAGES_SCHEMA_H