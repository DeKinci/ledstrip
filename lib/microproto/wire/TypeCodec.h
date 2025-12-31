#ifndef MICROPROTO_WIRE_TYPE_CODEC_H
#define MICROPROTO_WIRE_TYPE_CODEC_H

#include "Buffer.h"
#include "../TypeTraits.h"
#include "../PropertyBase.h"
#include "../MicroList.h"
#include "../Reflect.h"
#include "../Field.h"

namespace MicroProto {

// Forward declarations for container properties
class ArrayPropertyBase;
class ListPropertyBase;
struct ObjectFieldDef;
struct VariantTypeDef;
struct ResourceHeader;

/**
 * TypeCodec - Encode/decode values based on type ID
 *
 * Used to serialize property values to wire format and back.
 * Supports basic types and container types (ARRAY, LIST).
 */
class TypeCodec {
public:
    /**
     * Encode a basic type value to the buffer
     *
     * @param buf WriteBuffer to write to
     * @param typeId Type ID (from TypeTraits)
     * @param data Pointer to the value
     * @param size Size of the value in bytes
     * @return true if successful
     */
    static bool encodeBasic(WriteBuffer& buf, uint8_t typeId, const void* data, size_t size) {
        switch (typeId) {
            case TYPE_BOOL:
                if (size != 1) return false;
                return buf.writeBool(*static_cast<const bool*>(data));

            case TYPE_INT8:
                if (size != 1) return false;
                return buf.writeInt8(*static_cast<const int8_t*>(data));

            case TYPE_UINT8:
                if (size != 1) return false;
                return buf.writeUint8(*static_cast<const uint8_t*>(data));

            case TYPE_INT32:
                if (size != 4) return false;
                return buf.writeInt32(*static_cast<const int32_t*>(data));

            case TYPE_FLOAT32:
                if (size != 4) return false;
                return buf.writeFloat32(*static_cast<const float*>(data));

            default:
                return false;  // Unknown type
        }
    }

    /**
     * Encode a value to the buffer based on type ID (legacy alias)
     */
    static bool encode(WriteBuffer& buf, uint8_t typeId, const void* data, size_t size) {
        return encodeBasic(buf, typeId, data, size);
    }

    /**
     * Encode array elements (fixed count, no length prefix)
     *
     * @param buf WriteBuffer to write to
     * @param elementTypeId Element type ID
     * @param data Pointer to element data
     * @param count Number of elements
     * @param elementSize Size of each element
     * @return true if successful
     */
    static bool encodeArray(WriteBuffer& buf, uint8_t elementTypeId, const void* data,
                            size_t count, size_t elementSize) {
        const uint8_t* ptr = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < count; ++i) {
            if (!encodeBasic(buf, elementTypeId, ptr, elementSize)) {
                return false;
            }
            ptr += elementSize;
        }
        return true;
    }

    /**
     * Encode list elements (varint count prefix + elements)
     *
     * @param buf WriteBuffer to write to
     * @param elementTypeId Element type ID
     * @param data Pointer to element data
     * @param count Number of elements
     * @param elementSize Size of each element
     * @return true if successful
     */
    static bool encodeList(WriteBuffer& buf, uint8_t elementTypeId, const void* data,
                           size_t count, size_t elementSize) {
        // Write count as varint
        if (buf.writeVarint(static_cast<uint32_t>(count)) == 0) {
            return false;
        }
        // Write elements
        return encodeArray(buf, elementTypeId, data, count, elementSize);
    }

    /**
     * Encode a property value to the buffer
     * Handles both basic types and container types
     *
     * @param buf WriteBuffer to write to
     * @param prop Property to encode
     * @return true if successful
     */
    static bool encodeProperty(WriteBuffer& buf, const PropertyBase* prop);

    /**
     * Decode a basic type value from the buffer
     *
     * @param buf ReadBuffer to read from
     * @param typeId Type ID (from TypeTraits)
     * @param data Pointer to store the value
     * @param size Expected size of the value
     * @return true if successful
     */
    static bool decodeBasic(ReadBuffer& buf, uint8_t typeId, void* data, size_t size) {
        switch (typeId) {
            case TYPE_BOOL:
                if (size != 1) return false;
                *static_cast<bool*>(data) = buf.readBool();
                return buf.ok();

            case TYPE_INT8:
                if (size != 1) return false;
                *static_cast<int8_t*>(data) = buf.readInt8();
                return buf.ok();

            case TYPE_UINT8:
                if (size != 1) return false;
                *static_cast<uint8_t*>(data) = buf.readUint8();
                return buf.ok();

            case TYPE_INT32:
                if (size != 4) return false;
                *static_cast<int32_t*>(data) = buf.readInt32();
                return buf.ok();

            case TYPE_FLOAT32:
                if (size != 4) return false;
                *static_cast<float*>(data) = buf.readFloat32();
                return buf.ok();

            default:
                return false;  // Unknown type
        }
    }

    /**
     * Decode a value from the buffer based on type ID (legacy alias)
     */
    static bool decode(ReadBuffer& buf, uint8_t typeId, void* data, size_t size) {
        return decodeBasic(buf, typeId, data, size);
    }

    /**
     * Decode array elements (fixed count, no length prefix)
     *
     * @param buf ReadBuffer to read from
     * @param elementTypeId Element type ID
     * @param data Pointer to element storage
     * @param count Number of elements to read
     * @param elementSize Size of each element
     * @return true if successful
     */
    static bool decodeArray(ReadBuffer& buf, uint8_t elementTypeId, void* data,
                            size_t count, size_t elementSize) {
        uint8_t* ptr = static_cast<uint8_t*>(data);
        for (size_t i = 0; i < count; ++i) {
            if (!decodeBasic(buf, elementTypeId, ptr, elementSize)) {
                return false;
            }
            ptr += elementSize;
        }
        return true;
    }

    /**
     * Decode list elements (varint count prefix + elements)
     *
     * @param buf ReadBuffer to read from
     * @param elementTypeId Element type ID
     * @param data Pointer to element storage
     * @param maxCount Maximum elements to read
     * @param elementSize Size of each element
     * @param actualCount Output: actual number of elements read
     * @return true if successful
     */
    static bool decodeList(ReadBuffer& buf, uint8_t elementTypeId, void* data,
                           size_t maxCount, size_t elementSize, size_t& actualCount) {
        uint32_t count = buf.readVarint();
        if (!buf.ok()) return false;

        actualCount = (count < maxCount) ? count : maxCount;

        uint8_t* ptr = static_cast<uint8_t*>(data);
        for (size_t i = 0; i < actualCount; ++i) {
            if (!decodeBasic(buf, elementTypeId, ptr, elementSize)) {
                return false;
            }
            ptr += elementSize;
        }

        // Skip any remaining elements if count > maxCount
        for (size_t i = actualCount; i < count; ++i) {
            if (!buf.skip(elementSize)) {
                return false;
            }
        }

        return true;
    }

    /**
     * Decode a property value from the buffer
     * Handles both basic types and container types
     *
     * @param buf ReadBuffer to read from
     * @param prop Property to decode into
     * @return true if successful
     */
    static bool decodeProperty(ReadBuffer& buf, PropertyBase* prop);

    /**
     * Get the wire size of a type
     *
     * @param typeId Type ID
     * @return Size in bytes, 0 if unknown
     */
    static size_t typeSize(uint8_t typeId) {
        switch (typeId) {
            case TYPE_BOOL:   return 1;
            case TYPE_INT8:   return 1;
            case TYPE_UINT8:  return 1;
            case TYPE_INT32:  return 4;
            case TYPE_FLOAT32: return 4;
            default: return 0;
        }
    }

    /**
     * Get the wire size of a basic type
     */
    static size_t basicTypeSize(uint8_t typeId) {
        return typeSize(typeId);
    }

    /**
     * Get type name for debugging
     */
    static const char* typeName(uint8_t typeId) {
        switch (typeId) {
            case TYPE_BOOL:     return "BOOL";
            case TYPE_INT8:     return "INT8";
            case TYPE_UINT8:    return "UINT8";
            case TYPE_INT32:    return "INT32";
            case TYPE_FLOAT32:  return "FLOAT32";
            case TYPE_ARRAY:    return "ARRAY";
            case TYPE_LIST:     return "LIST";
            case TYPE_OBJECT:   return "OBJECT";
            case TYPE_VARIANT:  return "VARIANT";
            case TYPE_RESOURCE: return "RESOURCE";
            default: return "UNKNOWN";
        }
    }

    // =========== Recursive Template Encode/Decode ===========
    // These functions handle nested containers by recursively encoding/decoding
    // each element. Supports: basic types, std::array, MicroList, and structs.

    // --- Encode: Basic types ---
    template<typename T>
    static std::enable_if_t<is_microproto_basic_type_v<T> && !is_value_v<T>, bool>
    encode(WriteBuffer& buf, const T& value) {
        return encodeBasic(buf, TypeTraits<T>::type_id, &value, sizeof(T));
    }

    // --- Encode: Value<T> - encode only the value, not constraints ---
    template<typename T>
    static bool encode(WriteBuffer& buf, const Value<T>& val) {
        return encode(buf, val.value);
    }

    // --- Encode: Structs (trivially copyable, non-container) ---
    template<typename T>
    static std::enable_if_t<is_microproto_struct_v<T>, bool>
    encode(WriteBuffer& buf, const T& value) {
        return buf.writeBytes(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
    }

    // --- Encode: std::string ---
    // Wire format: varint(length) + UTF-8 bytes
    static bool encode(WriteBuffer& buf, const std::string& str) {
        if (buf.writeVarint(static_cast<uint32_t>(str.size())) == 0) {
            return false;
        }
        return buf.writeBytes(reinterpret_cast<const uint8_t*>(str.data()), str.size());
    }

    // --- Encode: std::array<T, N> ---
    template<typename T, size_t N>
    static bool encode(WriteBuffer& buf, const std::array<T, N>& arr) {
        for (size_t i = 0; i < N; ++i) {
            if (!encode(buf, arr[i])) return false;
        }
        return true;
    }

    // --- Encode: MicroList<T, I, M> ---
    template<typename T, size_t Inline, size_t Max>
    static bool encode(WriteBuffer& buf, const MicroList<T, Inline, Max>& list) {
        // Write count as varint
        if (buf.writeVarint(static_cast<uint32_t>(list.size())) == 0) {
            return false;
        }
        // Recursively encode each element
        for (size_t i = 0; i < list.size(); ++i) {
            if (!encode(buf, list[i])) return false;
        }
        return true;
    }

    // --- Decode: Basic types ---
    template<typename T>
    static std::enable_if_t<is_microproto_basic_type_v<T> && !is_value_v<T>, bool>
    decode(ReadBuffer& buf, T& value) {
        return decodeBasic(buf, TypeTraits<T>::type_id, &value, sizeof(T));
    }

    // --- Decode: Value<T> - decode into value, preserving constraints ---
    template<typename T>
    static bool decode(ReadBuffer& buf, Value<T>& val) {
        return decode(buf, val.value);
    }

    // --- Decode: Structs ---
    template<typename T>
    static std::enable_if_t<is_microproto_struct_v<T>, bool>
    decode(ReadBuffer& buf, T& value) {
        return buf.readBytes(reinterpret_cast<uint8_t*>(&value), sizeof(T));
    }

    // --- Decode: std::string ---
    // Wire format: varint(length) + UTF-8 bytes
    static bool decode(ReadBuffer& buf, std::string& str) {
        uint32_t length = buf.readVarint();
        if (!buf.ok()) return false;

        str.resize(length);
        return buf.readBytes(reinterpret_cast<uint8_t*>(str.data()), length);
    }

    // --- Decode: std::array<T, N> ---
    template<typename T, size_t N>
    static bool decode(ReadBuffer& buf, std::array<T, N>& arr) {
        for (size_t i = 0; i < N; ++i) {
            if (!decode(buf, arr[i])) return false;
        }
        return true;
    }

    // --- Decode: MicroList<T, I, M> ---
    template<typename T, size_t Inline, size_t Max>
    static bool decode(ReadBuffer& buf, MicroList<T, Inline, Max>& list) {
        // Read count
        uint32_t count = buf.readVarint();
        if (!buf.ok()) return false;

        // Limit to max capacity
        size_t actualCount = (Max > 0 && count > Max) ? Max : count;

        list.clear();

        // Recursively decode each element
        for (size_t i = 0; i < actualCount; ++i) {
            T value{};
            if (!decode(buf, value)) return false;
            list.push_back(std::move(value));
        }

        // Skip remaining elements if count > max
        // For variable-size elements, we can't easily skip, so just try to decode and discard
        for (size_t i = actualCount; i < count; ++i) {
            T temp{};
            if (!decode(buf, temp)) return false;
        }

        return true;
    }
};

// =========================================================================
// SchemaTypeEncoder - Encode DATA_TYPE_DEFINITION recursively
// =========================================================================
// Used for schema serialization. Encodes full type structure including
// nested containers. Uses compile-time type info via templates.

class SchemaTypeEncoder {
public:
    // --- Encode: Basic types ---
    template<typename T>
    static std::enable_if_t<is_microproto_basic_type_v<T>, bool>
    encode(WriteBuffer& buf, const ValueConstraints* constraints = nullptr) {
        if (!buf.writeByte(TypeTraits<T>::type_id)) return false;
        return encodeValueConstraints(buf, constraints, TypeTraits<T>::type_id);
    }

    // --- Encode: std::string → LIST<UINT8> per spec section 3.5 ---
    static bool encode(WriteBuffer& buf, const std::string*,
                       const ValueConstraints* = nullptr,
                       const ContainerConstraints* containerConstraints = nullptr) {
        // String is LIST of UINT8
        if (!buf.writeByte(TYPE_LIST)) return false;

        // Container constraints (length limits)
        if (!containerConstraints || !containerConstraints->any()) {
            if (!buf.writeByte(0)) return false;
        } else {
            if (!buf.writeByte(containerConstraints->encode())) return false;
            if (containerConstraints->hasMinLength) {
                if (buf.writeVarint(static_cast<uint32_t>(containerConstraints->minLength)) == 0) return false;
            }
            if (containerConstraints->hasMaxLength) {
                if (buf.writeVarint(static_cast<uint32_t>(containerConstraints->maxLength)) == 0) return false;
            }
        }

        // Element: UINT8 with no constraints
        if (!buf.writeByte(TYPE_UINT8)) return false;
        return buf.writeByte(0);  // No element constraints
    }

    // Overload for string type deduction
    static bool encodeType(WriteBuffer& buf, const std::string*,
                           const ValueConstraints* vc = nullptr,
                           const ContainerConstraints* cc = nullptr) {
        return encode(buf, static_cast<const std::string*>(nullptr), vc, cc);
    }

    // --- Encode: Structs (trivially copyable) → OBJECT with field types ---
    // Uses reflection to encode field count and types (names are empty - no runtime names available)
    template<typename T>
    static std::enable_if_t<is_microproto_struct_v<T>, bool>
    encode(WriteBuffer& buf, const ValueConstraints* = nullptr) {
        if (!buf.writeByte(TYPE_OBJECT)) return false;

        // Get field count via reflection
        constexpr size_t fieldCount = reflect::field_count_v<T>;
        if (buf.writeVarint(static_cast<uint32_t>(fieldCount)) == 0) return false;

        // Encode each field: ident name + DATA_TYPE_DEFINITION
        return encodeStructFieldsImpl<T>(buf, std::make_index_sequence<fieldCount>{});
    }

private:
    // Helper to encode struct fields using index sequence
    template<typename T, size_t... Is>
    static bool encodeStructFieldsImpl(WriteBuffer& buf, std::index_sequence<Is...>) {
        return (encodeStructField<T, Is>(buf) && ...);
    }

    // Encode a single field at index I
    template<typename T, size_t I>
    static bool encodeStructField(WriteBuffer& buf) {
        using RawFieldType = std::remove_cv_t<std::remove_reference_t<reflect::field_type_t<T, I>>>;
        // Unwrap Value<T> wrapper if present to get actual value type
        using FieldType = unwrap_value_t<RawFieldType>;

        // Write field name (ident: u8 length + ASCII bytes)
        // Uses MICROPROTO_FIELD_NAMES if registered, otherwise empty
        const char* fieldName = reflect::get_field_name<T>(I);
        if (fieldName != nullptr) {
            if (!buf.writeIdent(fieldName)) return false;
        } else {
            // No field names registered - write empty ident
            if (!buf.writeByte(0)) return false;
        }

        // Recursive DATA_TYPE_DEFINITION for this field's type
        return encodeElement<FieldType>(buf, nullptr);
    }

public:

    // --- Encode: std::array<T, N> → ARRAY ---
    template<typename T, size_t N>
    static bool encode(WriteBuffer& buf, const ValueConstraints* elementConstraints = nullptr) {
        if (!buf.writeByte(TYPE_ARRAY)) return false;
        if (buf.writeVarint(static_cast<uint32_t>(N)) == 0) return false;
        // Recursive: encode element type definition
        return encodeElement<T>(buf, elementConstraints);
    }

    // --- Encode: MicroList<T, I, M> → LIST ---
    template<typename T, size_t Inline, size_t Max>
    static bool encode(WriteBuffer& buf,
                       const ValueConstraints* elementConstraints = nullptr,
                       const ContainerConstraints* containerConstraints = nullptr) {
        if (!buf.writeByte(TYPE_LIST)) return false;

        // Container constraints
        if (!containerConstraints || !containerConstraints->any()) {
            if (!buf.writeByte(0)) return false;
        } else {
            if (!buf.writeByte(containerConstraints->encode())) return false;
            if (containerConstraints->hasMinLength) {
                if (buf.writeVarint(static_cast<uint32_t>(containerConstraints->minLength)) == 0) return false;
            }
            if (containerConstraints->hasMaxLength) {
                if (buf.writeVarint(static_cast<uint32_t>(containerConstraints->maxLength)) == 0) return false;
            }
        }

        // Recursive: encode element type definition
        return encodeElement<T>(buf, elementConstraints);
    }

    // --- Helper to encode element type (dispatches based on type) ---
    template<typename T>
    static std::enable_if_t<is_microproto_basic_type_v<T>, bool>
    encodeElement(WriteBuffer& buf, const ValueConstraints* constraints) {
        return encode<T>(buf, constraints);
    }

    template<typename T>
    static std::enable_if_t<is_microproto_struct_v<T>, bool>
    encodeElement(WriteBuffer& buf, const ValueConstraints*) {
        return encode<T>(buf, nullptr);
    }

    // Element is std::string
    template<typename T>
    static std::enable_if_t<is_microproto_string_v<T>, bool>
    encodeElement(WriteBuffer& buf, const ValueConstraints*) {
        return encode(buf, static_cast<const std::string*>(nullptr), nullptr, nullptr);
    }

    // Element is container (std::array or MicroList) - forward declared, defined below
    template<typename T>
    static std::enable_if_t<
        is_microproto_container_v<T> && !is_microproto_string_v<T>,
        bool
    >
    encodeElement(WriteBuffer& buf, const ValueConstraints*);

private:
    // Encode value constraints for basic types
    static bool encodeValueConstraints(WriteBuffer& buf, const ValueConstraints* constraints, uint8_t typeId) {
        if (!constraints || !constraints->flags.any()) {
            return buf.writeByte(0);
        }

        if (!buf.writeByte(constraints->flags.encode())) return false;

        size_t typeSize = TypeCodec::basicTypeSize(typeId);
        if (typeSize == 0) return true;

        if (constraints->flags.hasMin) {
            if (!buf.writeBytes(constraints->minValue, typeSize)) return false;
        }
        if (constraints->flags.hasMax) {
            if (!buf.writeBytes(constraints->maxValue, typeSize)) return false;
        }
        if (constraints->flags.hasStep) {
            if (!buf.writeBytes(constraints->stepValue, typeSize)) return false;
        }

        // Encode oneof values if present
        // Format: varint(count) + [value]...
        if (constraints->flags.hasOneOf && constraints->oneofCount > 0) {
            if (buf.writeVarint(static_cast<uint32_t>(constraints->oneofCount)) == 0) return false;
            for (size_t i = 0; i < constraints->oneofCount; i++) {
                // Each value is stored at offset i * MAX_SIZE in oneofValues
                if (!buf.writeBytes(&constraints->oneofValues[i * ValueConstraints::MAX_SIZE], typeSize)) {
                    return false;
                }
            }
        }

        return true;
    }
};

// --- Container element encoding helpers ---
// Forward declarations for container type dispatch
template<typename T, size_t N>
inline bool encodeContainerTypeImpl(WriteBuffer& buf, std::array<T, N>*) {
    return SchemaTypeEncoder::encode<T, N>(buf, nullptr);
}

template<typename T, size_t I, size_t M>
inline bool encodeContainerTypeImpl(WriteBuffer& buf, MicroList<T, I, M>*) {
    return SchemaTypeEncoder::encode<T, I, M>(buf, nullptr, nullptr);
}

// Specialization for container elements
template<typename T>
inline std::enable_if_t<
    is_microproto_container_v<T> && !is_microproto_string_v<T>,
    bool
>
SchemaTypeEncoder::encodeElement(WriteBuffer& buf, const ValueConstraints*) {
    // Dispatch to appropriate container encoder using tag dispatch
    return encodeContainerTypeImpl(buf, static_cast<T*>(nullptr));
}

} // namespace MicroProto

#endif // MICROPROTO_WIRE_TYPE_CODEC_H