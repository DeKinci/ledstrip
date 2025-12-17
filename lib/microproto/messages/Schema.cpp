#include "Schema.h"
#include <string.h>

namespace MicroProto {

bool SchemaEncoder::encodeProperty(WriteBuffer& buf, const PropertyBase* prop) {
    OpHeader header(OpCode::SCHEMA_UPSERT);
    if (!buf.writeByte(header.encode())) return false;

    return encodePropertyItem(buf, prop);
}

size_t SchemaEncoder::encodeAllProperties(WriteBuffer& buf) {
    size_t count = PropertyBase::count;
    if (count == 0) return 0;
    if (count > 256) count = 256;

    OpHeader header(OpCode::SCHEMA_UPSERT, 0, true);
    if (!buf.writeByte(header.encode())) return 0;

    if (!buf.writeByte(static_cast<uint8_t>(count - 1))) return 0;

    size_t encoded = 0;
    for (uint8_t i = 0; i < count; i++) {
        PropertyBase* prop = PropertyBase::byId[i];
        if (!prop || !encodePropertyItem(buf, prop)) return 0;
        encoded++;
    }

    return encoded;
}

// Helper to get the wire size of a basic type (per spec section 3.1)
static size_t getTypeWireSize(uint8_t typeId) {
    switch (typeId) {
        case TYPE_BOOL: return 1;
        case TYPE_INT8: return 1;
        case TYPE_UINT8: return 1;
        case TYPE_INT32: return 4;
        case TYPE_FLOAT32: return 4;
        default: return 0;
    }
}

// Helper to encode value constraints (validation_flags + optional min/max/step)
static bool encodeValueConstraints(WriteBuffer& buf, const ValueConstraints* constraints, uint8_t typeId) {
    if (!constraints || !constraints->flags.any()) {
        // No constraints
        if (!buf.writeByte(0)) return false;
        return true;
    }

    // Write validation_flags
    if (!buf.writeByte(constraints->flags.encode())) return false;

    // Get type size for raw value encoding
    size_t typeSize = getTypeWireSize(typeId);
    if (typeSize == 0) return true;  // Unknown type, skip values

    // Write min value if present
    if (constraints->flags.hasMin) {
        if (!buf.writeBytes(constraints->minValue, typeSize)) return false;
    }

    // Write max value if present
    if (constraints->flags.hasMax) {
        if (!buf.writeBytes(constraints->maxValue, typeSize)) return false;
    }

    // Write step value if present
    if (constraints->flags.hasStep) {
        if (!buf.writeBytes(constraints->stepValue, typeSize)) return false;
    }

    return true;
}

// Helper to encode DATA_TYPE_DEFINITION for a property
static bool encodeTypeDefinition(WriteBuffer& buf, const PropertyBase* prop) {
    uint8_t typeId = prop->getTypeId();

    if (typeId == TYPE_ARRAY) {
        // ARRAY: type_id + varint(element_count) + element DATA_TYPE_DEFINITION
        if (!buf.writeByte(TYPE_ARRAY)) return false;
        if (!buf.writeVarint(static_cast<uint32_t>(prop->getElementCount()))) return false;

        // Element DATA_TYPE_DEFINITION: element_type_id + element_constraints
        if (!buf.writeByte(prop->getElementTypeId())) return false;
        if (!encodeValueConstraints(buf, prop->getElementConstraints(), prop->getElementTypeId())) return false;
        return true;
    }

    if (typeId == TYPE_LIST) {
        // LIST: type_id + length_constraints + element DATA_TYPE_DEFINITION
        if (!buf.writeByte(TYPE_LIST)) return false;

        // Encode container constraints
        const ContainerConstraints* cc = prop->getContainerConstraints();
        if (!cc || !cc->any()) {
            // No container constraints
            if (!buf.writeByte(0)) return false;
        } else {
            // Write constraint flags
            if (!buf.writeByte(cc->encode())) return false;

            // Write minLength if present
            if (cc->hasMinLength) {
                if (!buf.writeVarint(static_cast<uint32_t>(cc->minLength))) return false;
            }

            // Write maxLength if present
            if (cc->hasMaxLength) {
                if (!buf.writeVarint(static_cast<uint32_t>(cc->maxLength))) return false;
            }
        }

        // Element DATA_TYPE_DEFINITION: element_type_id + element_constraints
        if (!buf.writeByte(prop->getElementTypeId())) return false;
        if (!encodeValueConstraints(buf, prop->getElementConstraints(), prop->getElementTypeId())) return false;
        return true;
    }

    // Basic type: type_id + value_constraints
    if (!buf.writeByte(typeId)) return false;
    if (!encodeValueConstraints(buf, prop->getValueConstraints(), typeId)) return false;
    return true;
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

    // Description (varint length + bytes, or 0 if no description)
    if (prop->description) {
        size_t descLen = strlen(prop->description);
        if (descLen > 255) descLen = 255;
        if (!buf.writeVarint(static_cast<uint32_t>(descLen))) return false;
        if (!buf.writeBytes(reinterpret_cast<const uint8_t*>(prop->description), descLen)) return false;
    } else {
        if (!buf.writeVarint(0)) return false;
    }

    // Encode DATA_TYPE_DEFINITION (handles basic and container types)
    if (!encodeTypeDefinition(buf, prop)) return false;

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

    OpHeader header(OpCode::PROPERTY_UPDATE_SHORT, 0, true);
    if (!buf.writeByte(header.encode())) return 0;

    if (!buf.writeByte(static_cast<uint8_t>(count - 1))) return 0;

    size_t encoded = 0;
    for (uint8_t i = 0; i < count; i++) {
        PropertyBase* prop = PropertyBase::byId[i];
        if (!prop) continue;

        if (!buf.writeByte(prop->id)) return 0;

        PropertyUpdateFlags flags;
        if (!buf.writeByte(flags.encode())) return 0;

        if (!TypeCodec::encodeProperty(buf, prop)) return 0;

        encoded++;
    }

    return encoded;
}

} // namespace MicroProto