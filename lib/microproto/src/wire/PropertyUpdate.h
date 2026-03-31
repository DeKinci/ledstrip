#ifndef MICROPROTO_WIRE_PROPERTY_UPDATE_H
#define MICROPROTO_WIRE_PROPERTY_UPDATE_H

#include "Buffer.h"
#include "OpCode.h"
#include "TypeCodec.h"
#include "../PropertyBase.h"

namespace MicroProto {

/**
 * PropertyUpdate - Encode/decode PROPERTY_UPDATE messages
 *
 * Wire format (MVP spec):
 *   u8 operation_header { opcode: 0x1, flags: bit0=batch, bit1=has_timestamp }
 *   [u8 batch_count]         // If batch=1 (count-1, so 0 means 1 item)
 *   [varint timestamp]       // If has_timestamp=1 (once for entire batch)
 *
 *   // For each property update:
 *   propid property_id       // 1-2 bytes (0-127 = 1 byte, 128-32767 = 2 bytes)
 *   [varint version]         // If property.level != LOCAL
 *   [varint source_node_id]  // If property.level != LOCAL
 *   bytes value              // Encoded according to property's type
 *
 * Note: For MVP, all properties are LOCAL (no version/source_node_id).
 */
class PropertyUpdate {
public:
    /**
     * Encode a single property update
     *
     * @param buf WriteBuffer to write to
     * @param prop Property to encode
     * @return true if successful
     */
    static bool encode(WriteBuffer& buf, const PropertyBase* prop) {
        // Operation header: opcode=1 (PROPERTY_UPDATE), flags=0
        if (!buf.writeByte(encodeOpHeader(OpCode::PROPERTY_UPDATE, 0))) return false;

        // Property ID (propid encoding: 1-2 bytes)
        if (!buf.writePropId(prop->id)) return false;

        // Version fields (only for GROUP/GLOBAL - skipped for MVP LOCAL)
        // For MVP: all properties are LOCAL, no version fields

        // Value
        return TypeCodec::encodeProperty(buf, prop);
    }

    /**
     * Encode a single property update with timestamp
     */
    static bool encodeWithTimestamp(WriteBuffer& buf, const PropertyBase* prop, uint32_t timestamp) {
        PropertyUpdateFlags flags;
        flags.hasTimestamp = true;

        if (!buf.writeByte(encodeOpHeader(OpCode::PROPERTY_UPDATE, flags.encode()))) return false;
        if (buf.writeVarint(timestamp) == 0) return false;
        if (!buf.writePropId(prop->id)) return false;

        return TypeCodec::encodeProperty(buf, prop);
    }

    /**
     * Encode a single property update with explicit value
     *
     * @param buf WriteBuffer to write to
     * @param propertyId Property ID (0-32767)
     * @param typeId Type ID
     * @param data Pointer to value
     * @param size Size of value
     * @return true if successful
     */
    static bool encodeValue(WriteBuffer& buf, uint16_t propertyId,
                            uint8_t typeId, const void* data, size_t size) {
        if (!buf.writeByte(encodeOpHeader(OpCode::PROPERTY_UPDATE, 0))) return false;
        if (!buf.writePropId(propertyId)) return false;

        return TypeCodec::encode(buf, typeId, data, size);
    }

    /**
     * Encode multiple property updates (batched)
     *
     * @param buf WriteBuffer to write to
     * @param props Array of property pointers
     * @param count Number of properties (1-256)
     * @return true if successful
     */
    static bool encodeBatch(WriteBuffer& buf, const PropertyBase** props, size_t count) {
        if (count == 0 || count > 256) return false;

        // Operation header with batch flag
        PropertyUpdateFlags flags;
        flags.batch = true;

        if (!buf.writeByte(encodeOpHeader(OpCode::PROPERTY_UPDATE, flags.encode()))) return false;

        // Batch count (value is count-1, so 0 means 1 operation)
        if (!buf.writeByte(static_cast<uint8_t>(count - 1))) return false;

        // Each property
        for (size_t i = 0; i < count; i++) {
            const PropertyBase* prop = props[i];
            if (!buf.writePropId(prop->id)) return false;
            if (!TypeCodec::encodeProperty(buf, prop)) return false;
        }

        return true;
    }

    /**
     * Encode multiple property updates with timestamp (batched)
     */
    static bool encodeBatchWithTimestamp(WriteBuffer& buf, const PropertyBase** props,
                                          size_t count, uint32_t timestamp) {
        if (count == 0 || count > 256) return false;

        PropertyUpdateFlags flags;
        flags.batch = true;
        flags.hasTimestamp = true;

        if (!buf.writeByte(encodeOpHeader(OpCode::PROPERTY_UPDATE, flags.encode()))) return false;
        if (!buf.writeByte(static_cast<uint8_t>(count - 1))) return false;
        if (buf.writeVarint(timestamp) == 0) return false;

        for (size_t i = 0; i < count; i++) {
            const PropertyBase* prop = props[i];
            if (!buf.writePropId(prop->id)) return false;
            if (!TypeCodec::encodeProperty(buf, prop)) return false;
        }

        return true;
    }

    /**
     * Decode header and get batch info
     *
     * @param buf ReadBuffer (assumes header byte already consumed)
     * @param flags Decoded flags
     * @param outBatchCount Output: number of items (1 if not batched)
     * @param outTimestamp Output: timestamp if has_timestamp flag set
     * @return true if successful
     */
    static bool decodeHeader(uint8_t flagBits, ReadBuffer& buf,
                             uint8_t& outBatchCount, uint32_t& outTimestamp) {
        PropertyUpdateFlags flags = PropertyUpdateFlags::decode(flagBits);

        if (flags.batch) {
            outBatchCount = buf.readByte() + 1;  // Stored as count-1
            if (!buf.ok()) return false;
        } else {
            outBatchCount = 1;
        }

        if (flags.hasTimestamp) {
            outTimestamp = buf.readVarint();
            if (!buf.ok()) return false;
        } else {
            outTimestamp = 0;
        }

        return true;
    }

    /**
     * Decode a single property update item (after header)
     *
     * @param buf ReadBuffer
     * @param outPropertyId Output: property ID (0-32767)
     * @param outValue Output buffer for value (must be large enough)
     * @param outValueSize Output: size of decoded value
     * @param typeId Type ID of the property (caller must look up from schema)
     * @param level Property level (determines if version fields present)
     * @return true if successful
     */
    static bool decodeItem(ReadBuffer& buf, uint16_t& outPropertyId,
                           void* outValue, size_t& outValueSize,
                           uint8_t typeId, PropertyLevel level = PropertyLevel::LOCAL) {
        // Property ID (propid encoding)
        outPropertyId = buf.readPropId();
        if (!buf.ok()) return false;

        // Version fields (only for GROUP/GLOBAL)
        if (level != PropertyLevel::LOCAL) {
            buf.readVarint();  // version
            buf.readVarint();  // source_node_id
            if (!buf.ok()) return false;
        }

        // Value
        outValueSize = TypeCodec::typeSize(typeId);
        return TypeCodec::decode(buf, typeId, outValue, outValueSize);
    }

    /**
     * Decode a property update and apply to property registry
     *
     * @param buf ReadBuffer (positioned after header)
     * @param outPropertyId Output: property ID that was updated
     * @return true if property was found and updated successfully
     */
    static bool decodeAndApply(ReadBuffer& buf, uint16_t& outPropertyId) {
        outPropertyId = buf.readPropId();
        if (!buf.ok()) return false;

        PropertyBase* prop = PropertyBase::find(static_cast<uint8_t>(outPropertyId));
        if (!prop) return false;

        // For MVP: all LOCAL, no version fields
        // TODO: Check prop->level for GROUP/GLOBAL

        return TypeCodec::decodeProperty(buf, prop);
    }
};

} // namespace MicroProto

#endif // MICROPROTO_WIRE_PROPERTY_UPDATE_H