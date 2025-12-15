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
 * Wire format (PROPERTY_UPDATE_SHORT, non-batched):
 *   u8 operation_header (opcode=0x1, flags=0, batch=0)
 *   u8 property_id
 *   u8 update_flags
 *   [optional fields based on flags]
 *   bytes value
 */
class PropertyUpdate {
public:
    /**
     * Encode a single property update (SHORT format, u8 ID)
     *
     * @param buf WriteBuffer to write to
     * @param prop Property to encode
     * @return true if successful
     */
    static bool encodeShort(WriteBuffer& buf, const PropertyBase* prop) {
        // Operation header: opcode=1 (PROPERTY_UPDATE_SHORT), flags=0, batch=0
        OpHeader header(OpCode::PROPERTY_UPDATE_SHORT);
        if (!buf.writeByte(header.encode())) return false;

        // Property ID (u8)
        if (!buf.writeByte(prop->id)) return false;

        // Update flags (no timestamp/version for simple updates)
        PropertyUpdateFlags flags;
        if (!buf.writeByte(flags.encode())) return false;

        // Value
        return TypeCodec::encodeProperty(buf, prop);
    }

    /**
     * Encode a single property update with explicit value
     *
     * @param buf WriteBuffer to write to
     * @param propertyId Property ID
     * @param typeId Type ID
     * @param data Pointer to value
     * @param size Size of value
     * @return true if successful
     */
    static bool encodeShortValue(WriteBuffer& buf, uint8_t propertyId,
                                  uint8_t typeId, const void* data, size_t size) {
        OpHeader header(OpCode::PROPERTY_UPDATE_SHORT);
        if (!buf.writeByte(header.encode())) return false;
        if (!buf.writeByte(propertyId)) return false;

        PropertyUpdateFlags flags;
        if (!buf.writeByte(flags.encode())) return false;

        return TypeCodec::encode(buf, typeId, data, size);
    }

    /**
     * Encode multiple property updates (batched SHORT format)
     *
     * @param buf WriteBuffer to write to
     * @param props Array of property pointers
     * @param count Number of properties
     * @return true if successful
     */
    static bool encodeBatchShort(WriteBuffer& buf, const PropertyBase** props, size_t count) {
        if (count == 0 || count > 256) return false;

        // Operation header with batch flag
        OpHeader header(OpCode::PROPERTY_UPDATE_SHORT, 0, true);
        if (!buf.writeByte(header.encode())) return false;

        // Batch count (value is count-1, so 0 means 1 operation)
        if (!buf.writeByte(static_cast<uint8_t>(count - 1))) return false;

        // Each property
        for (size_t i = 0; i < count; i++) {
            const PropertyBase* prop = props[i];
            if (!buf.writeByte(prop->id)) return false;

            PropertyUpdateFlags flags;
            if (!buf.writeByte(flags.encode())) return false;

            if (!TypeCodec::encodeProperty(buf, prop)) return false;
        }

        return true;
    }

    /**
     * Decode a property update message
     *
     * @param buf ReadBuffer to read from
     * @param outPropertyId Output: property ID
     * @param outFlags Output: update flags
     * @param outValue Output buffer for value (must be large enough)
     * @param outValueSize Output: size of decoded value
     * @param typeId Type ID of the property (caller must know this)
     * @return true if successful
     */
    static bool decodeShort(ReadBuffer& buf, uint8_t& outPropertyId,
                            PropertyUpdateFlags& outFlags,
                            void* outValue, size_t& outValueSize,
                            uint8_t typeId) {
        // Read operation header
        uint8_t headerByte = buf.readByte();
        if (!buf.ok()) return false;

        OpHeader header = OpHeader::decode(headerByte);
        if (header.getOpCode() != OpCode::PROPERTY_UPDATE_SHORT) return false;
        if (header.batch) return false;  // Use decodeBatch for batched

        // Property ID
        outPropertyId = buf.readByte();
        if (!buf.ok()) return false;

        // Flags
        outFlags = PropertyUpdateFlags::decode(buf.readByte());
        if (!buf.ok()) return false;

        // Skip optional fields (timestamp, version) - not implemented yet
        if (outFlags.has_timestamp) {
            buf.skip(4);  // u32 timestamp
        }
        if (outFlags.has_version) {
            buf.skip(8);  // u32 version + u32 source_node_id
        }
        if (!buf.ok()) return false;

        // Value
        outValueSize = TypeCodec::typeSize(typeId);
        return TypeCodec::decode(buf, typeId, outValue, outValueSize);
    }

    /**
     * Decode header only, to determine batch status
     *
     * @param buf ReadBuffer (position will be advanced by 1 byte)
     * @param outHeader Output header
     * @param outBatchCount Output: number of items (1 if not batched)
     * @return true if valid PROPERTY_UPDATE_SHORT
     */
    static bool decodeHeader(ReadBuffer& buf, OpHeader& outHeader, uint8_t& outBatchCount) {
        uint8_t headerByte = buf.readByte();
        if (!buf.ok()) return false;

        outHeader = OpHeader::decode(headerByte);
        if (outHeader.getOpCode() != OpCode::PROPERTY_UPDATE_SHORT &&
            outHeader.getOpCode() != OpCode::PROPERTY_UPDATE_LONG) {
            return false;
        }

        if (outHeader.batch) {
            outBatchCount = buf.readByte() + 1;  // Stored as count-1
            return buf.ok();
        } else {
            outBatchCount = 1;
            return true;
        }
    }
};

} // namespace MicroProto

#endif // MICROPROTO_WIRE_PROPERTY_UPDATE_H