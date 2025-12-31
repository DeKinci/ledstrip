#include "Schema.h"
#include <string.h>

namespace MicroProto {

bool SchemaEncoder::encodeProperty(WriteBuffer& buf, const PropertyBase* prop) {
    // Single schema upsert (not batched)
    if (!buf.writeByte(encodeOpHeader(OpCode::SCHEMA_UPSERT, 0))) return false;

    return encodePropertyItem(buf, prop);
}

size_t SchemaEncoder::encodeAllProperties(WriteBuffer& buf) {
    size_t count = PropertyBase::count;
    if (count == 0) return 0;
    if (count > 256) count = 256;

    // Batched schema upsert
    if (!buf.writeByte(encodeOpHeader(OpCode::SCHEMA_UPSERT, Flags::BATCH))) return false;

    if (!buf.writeByte(static_cast<uint8_t>(count - 1))) return 0;

    size_t encoded = 0;
    for (uint8_t i = 0; i < count; i++) {
        PropertyBase* prop = PropertyBase::byId[i];
        if (!prop || !encodePropertyItem(buf, prop)) return 0;
        encoded++;
    }

    return encoded;
}

// Note: Type definition encoding is now handled by PropertyBase::encodeTypeDefinition()
// which uses compile-time type information via SchemaTypeEncoder for full recursive support.

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

    // Property ID using propid encoding
    if (!buf.writePropId(prop->id)) return false;

    // Namespace ID (propid, 0 = root)
    if (!buf.writePropId(0)) return false;

    // Name (ident: u8 length + bytes)
    if (!buf.writeIdent(prop->name)) return false;

    // Description (utf8: varint length + bytes)
    if (!buf.writeUtf8(prop->description)) return false;

    // Encode DATA_TYPE_DEFINITION using compile-time type info
    if (!prop->encodeTypeDefinition(buf)) return false;

    // Encode default value
    if (!TypeCodec::encodeProperty(buf, prop)) return false;

    // Encode UI hints (colorgroup is in upper 4 bits of flags)
    const UIHints& ui = prop->ui;
    uint8_t uiFlags = ui.encodeFlags();
    if (!buf.writeByte(uiFlags)) return false;

    // Order per spec: widget, unit, icon (color is in flags byte)
    if (ui.hasWidget()) {
        if (!buf.writeByte(ui.widget)) return false;
    }

    if (ui.hasUnit()) {
        size_t unitLen = strlen(ui.unit);
        if (unitLen > 255) unitLen = 255;
        if (!buf.writeByte(static_cast<uint8_t>(unitLen))) return false;
        if (!buf.writeBytes(reinterpret_cast<const uint8_t*>(ui.unit), unitLen)) return false;
    }

    if (ui.hasIcon()) {
        size_t iconLen = strlen(ui.icon);
        if (iconLen > 255) iconLen = 255;
        if (!buf.writeByte(static_cast<uint8_t>(iconLen))) return false;
        if (!buf.writeBytes(reinterpret_cast<const uint8_t*>(ui.icon), iconLen)) return false;
    }

    return true;
}

size_t PropertyEncoder::encodeAllValues(WriteBuffer& buf) {
    size_t count = PropertyBase::count;
    if (count == 0) return 0;
    if (count > 256) count = 256;

    // Batched property update
    PropertyUpdateFlags flags;
    flags.batch = true;

    if (!buf.writeByte(encodeOpHeader(OpCode::PROPERTY_UPDATE, flags.encode()))) return 0;
    if (!buf.writeByte(static_cast<uint8_t>(count - 1))) return 0;

    size_t encoded = 0;
    for (uint8_t i = 0; i < count; i++) {
        PropertyBase* prop = PropertyBase::byId[i];
        if (!prop) continue;

        // Property ID using propid encoding
        if (!buf.writePropId(prop->id)) return 0;

        // Value (no flags per property in MVP)
        if (!TypeCodec::encodeProperty(buf, prop)) return 0;

        encoded++;
    }

    return encoded;
}

// =========== SCHEMA_DELETE Encoder ===========

bool SchemaDeleteEncoder::encodePropertyDelete(WriteBuffer& buf, uint16_t propertyId) {
    // Single deletion (not batched)
    if (!buf.writeByte(encodeOpHeader(OpCode::SCHEMA_DELETE, 0))) return false;

    // Item type: PROPERTY (1) in lower 4 bits
    if (!buf.writeByte(static_cast<uint8_t>(SchemaItemType::PROPERTY))) return false;

    // Property ID using propid encoding
    if (!buf.writePropId(propertyId)) return false;

    return true;
}

bool SchemaDeleteEncoder::encodeBatchedDelete(WriteBuffer& buf, const uint16_t* propertyIds, size_t count) {
    if (count == 0 || count > 256) return false;

    // Batched deletion
    if (!buf.writeByte(encodeOpHeader(OpCode::SCHEMA_DELETE, Flags::BATCH))) return false;

    // Batch count (count - 1)
    if (!buf.writeByte(static_cast<uint8_t>(count - 1))) return false;

    // Each deletion item
    for (size_t i = 0; i < count; i++) {
        // Item type: PROPERTY (1)
        if (!buf.writeByte(static_cast<uint8_t>(SchemaItemType::PROPERTY))) return false;

        // Property ID
        if (!buf.writePropId(propertyIds[i])) return false;
    }

    return true;
}

// =========== SCHEMA_DELETE Decoder ===========

bool SchemaDeleteDecoder::decode(ReadBuffer& buf, uint8_t flags, DeleteItem* items,
                                  size_t maxItems, size_t& itemCount) {
    bool batched = flags & Flags::BATCH;

    size_t count = 1;
    if (batched) {
        uint8_t batchCount = buf.readByte();
        if (!buf.ok()) return false;
        count = static_cast<size_t>(batchCount) + 1;
    }

    if (count > maxItems) count = maxItems;
    itemCount = 0;

    for (size_t i = 0; i < count; i++) {
        uint8_t itemTypeByte = buf.readByte();
        if (!buf.ok()) return false;

        uint16_t itemId = buf.readPropId();
        if (!buf.ok()) return false;

        items[itemCount].type = static_cast<SchemaItemType>(itemTypeByte & 0x0F);
        items[itemCount].itemId = itemId;
        itemCount++;
    }

    return true;
}

} // namespace MicroProto