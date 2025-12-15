#include "Schema.h"
#include <string.h>

namespace MicroProto {

bool SchemaEncoder::encodeProperty(WriteBuffer& buf, const PropertyBase* prop) {
    OpHeader header(OpCode::SCHEMA_UPSERT);
    if (!buf.writeByte(header.encode())) return false;

    return encodePropertyItem(buf, prop);
}

size_t SchemaEncoder::encodeAllProperties(WriteBuffer& buf) {
    size_t count = 0;
    for (PropertyBase* p = PropertyBase::head; p; p = p->next) {
        count++;
    }

    if (count == 0) return 0;
    if (count > 256) count = 256;

    OpHeader header(OpCode::SCHEMA_UPSERT, 0, true);
    if (!buf.writeByte(header.encode())) return 0;

    if (!buf.writeByte(static_cast<uint8_t>(count - 1))) return 0;

    size_t encoded = 0;
    for (PropertyBase* prop = PropertyBase::head; prop && encoded < count; prop = prop->next) {
        if (!encodePropertyItem(buf, prop)) return 0;
        encoded++;
    }

    return encoded;
}

bool SchemaEncoder::encodePropertyItem(WriteBuffer& buf, const PropertyBase* prop) {
    uint8_t itemType = static_cast<uint8_t>(SchemaItemType::PROPERTY);
    if (prop->readonly) itemType |= (1 << 4);
    if (prop->persistent) itemType |= (1 << 5);
    if (prop->hidden) itemType |= (1 << 6);
    if (!buf.writeByte(itemType)) return false;

    uint8_t levelFlags = static_cast<uint8_t>(prop->level);
    if (prop->ble_exposed) levelFlags |= (1 << 2);
    if (!buf.writeByte(levelFlags)) return false;

    if (prop->level == PropertyLevel::GROUP) {
        if (!buf.writeByte(prop->group_id)) return false;
    }

    if (!buf.writeByte(prop->id)) return false;
    if (!buf.writeByte(0)) return false;  // Namespace ID

    size_t nameLen = strlen(prop->name);
    if (nameLen > 255) nameLen = 255;
    if (!buf.writeByte(static_cast<uint8_t>(nameLen))) return false;
    if (!buf.writeBytes(reinterpret_cast<const uint8_t*>(prop->name), nameLen)) return false;

    if (!buf.writeVarint(0)) return false;  // Description

    if (!buf.writeByte(prop->getTypeId())) return false;
    if (!buf.writeByte(0)) return false;  // No constraints

    if (!TypeCodec::encodeProperty(buf, prop)) return false;

    if (!buf.writeByte(0)) return false;  // No UI hints

    return true;
}

size_t PropertyEncoder::encodeAllValues(WriteBuffer& buf) {
    size_t count = 0;
    for (PropertyBase* p = PropertyBase::head; p; p = p->next) {
        count++;
    }

    if (count == 0) return 0;
    if (count > 256) count = 256;

    OpHeader header(OpCode::PROPERTY_UPDATE_SHORT, 0, true);
    if (!buf.writeByte(header.encode())) return 0;

    if (!buf.writeByte(static_cast<uint8_t>(count - 1))) return 0;

    size_t encoded = 0;
    for (PropertyBase* prop = PropertyBase::head; prop && encoded < count; prop = prop->next) {
        if (!buf.writeByte(prop->id)) return 0;

        PropertyUpdateFlags flags;
        if (!buf.writeByte(flags.encode())) return 0;

        if (!TypeCodec::encodeProperty(buf, prop)) return 0;

        encoded++;
    }

    return encoded;
}

} // namespace MicroProto