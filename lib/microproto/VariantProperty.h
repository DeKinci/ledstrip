#ifndef MICROPROTO_VARIANT_PROPERTY_H
#define MICROPROTO_VARIANT_PROPERTY_H

#include "PropertyBase.h"
#include "TypeTraits.h"
#include "wire/Buffer.h"
#include <array>
#include <string.h>

namespace MicroProto {

/**
 * VariantTypeDef - Describes one possible type in a VARIANT
 *
 * Each variant option has a human-readable name and type definition.
 * For MVP, we support basic types only (composites can be added later).
 */
struct VariantTypeDef {
    const char* name;           // Human-readable name (UTF-8)
    uint8_t typeId;             // TYPE_BOOL, TYPE_UINT8, TYPE_INT32, etc.
    uint16_t size;              // Size of this type in bytes
    ValueConstraints constraints;  // Optional constraints for this type

    VariantTypeDef()
        : name(nullptr), typeId(0), size(0) {}

    VariantTypeDef(const char* n, uint8_t type, uint16_t sz)
        : name(n), typeId(type), size(sz) {}

    VariantTypeDef(const char* n, uint8_t type, uint16_t sz, const ValueConstraints& c)
        : name(n), typeId(type), size(sz), constraints(c) {}
};

/**
 * VariantProperty - Tagged union property
 *
 * VARIANT (0x23) can hold one of several types at any time.
 * Wire format: u8 type_index + value bytes
 * Schema: type_id(0x23) + type_count + for each type: (utf8 name + DATA_TYPE_DEFINITION)
 *
 * Template parameters:
 *   TypeCount - Number of possible types (2-255)
 *   MaxDataSize - Maximum size needed for largest type's value
 *
 * Usage:
 *   // Result variant: success (UINT8) or error (OBJECT)
 *   VariantProperty<2, 64> result("result",
 *       {
 *           VariantTypeDef("value", TYPE_UINT8, 1),
 *           VariantTypeDef("error", TYPE_INT32, 4)  // Simplified error code
 *       },
 *       PropertyLevel::LOCAL);
 *
 *   // Set to "value" type
 *   result.set<uint8_t>(0, 42);
 *
 *   // Set to "error" type
 *   result.set<int32_t>(1, -1);
 *
 *   // Check current type
 *   if (result.typeIndex() == 0) {
 *       uint8_t val = result.get<uint8_t>();
 *   }
 */
template<size_t TypeCount, size_t MaxDataSize>
class VariantProperty : public PropertyBase {
public:
    static_assert(TypeCount >= 2, "VariantProperty must have at least 2 types");
    static_assert(TypeCount <= 255, "VariantProperty supports max 255 types");
    static_assert(MaxDataSize > 0, "VariantProperty must have non-zero data size");

    using TypeArray = std::array<VariantTypeDef, TypeCount>;

    /**
     * Constructor with type definitions
     */
    VariantProperty(
        const char* name,
        const TypeArray& types,
        PropertyLevel level = PropertyLevel::LOCAL,
        const char* description = nullptr,
        UIHints uiHints = UIHints(),
        bool persistent = false,
        bool readonly = false,
        bool hidden = false,
        bool ble_exposed = false,
        uint8_t group_id = 0
    ) : PropertyBase(name, level, persistent, readonly, hidden, ble_exposed, group_id, description, uiHints),
        _types(types),
        _currentType(0)
    {
        _data.fill(0);
    }

    /**
     * Constructor with initial type and value
     */
    VariantProperty(
        const char* name,
        const TypeArray& types,
        uint8_t initialTypeIndex,
        const std::array<uint8_t, MaxDataSize>& initialData,
        PropertyLevel level = PropertyLevel::LOCAL,
        const char* description = nullptr,
        UIHints uiHints = UIHints(),
        bool persistent = false,
        bool readonly = false,
        bool hidden = false,
        bool ble_exposed = false,
        uint8_t group_id = 0
    ) : PropertyBase(name, level, persistent, readonly, hidden, ble_exposed, group_id, description, uiHints),
        _types(types),
        _currentType(initialTypeIndex < TypeCount ? initialTypeIndex : 0),
        _data(initialData)
    {
    }

    // =========== Type access ===========

    /**
     * Get number of possible types
     */
    constexpr size_t typeCount() const {
        return TypeCount;
    }

    /**
     * Get type definition by index
     */
    const VariantTypeDef& getTypeDef(size_t index) const {
        return _types[index];
    }

    /**
     * Find type index by name, returns TypeCount if not found
     */
    size_t findType(const char* typeName) const {
        for (size_t i = 0; i < TypeCount; i++) {
            if (_types[i].name && strcmp(_types[i].name, typeName) == 0) {
                return i;
            }
        }
        return TypeCount;  // Not found
    }

    /**
     * Get current type index
     */
    uint8_t typeIndex() const {
        return _currentType;
    }

    /**
     * Get current type definition
     */
    const VariantTypeDef& currentTypeDef() const {
        return _types[_currentType];
    }

    /**
     * Check if current type matches given index
     */
    bool is(uint8_t typeIndex) const {
        return _currentType == typeIndex;
    }

    /**
     * Check if current type matches given name
     */
    bool is(const char* typeName) const {
        size_t index = findType(typeName);
        return index != TypeCount && _currentType == index;
    }

    // =========== Value access ===========

    /**
     * Get typed value (assumes current type matches T)
     */
    template<typename T>
    T get() const {
        const VariantTypeDef& type = _types[_currentType];
        if (sizeof(T) > type.size) return T{};

        T value;
        memcpy(&value, _data.data(), sizeof(T));
        return value;
    }

    /**
     * Set value with type index
     */
    template<typename T>
    bool set(uint8_t typeIndex, const T& value) {
        if (readonly) return false;
        if (typeIndex >= TypeCount) return false;

        const VariantTypeDef& type = _types[typeIndex];
        if (sizeof(T) > type.size) return false;

        // Check if anything changed
        if (_currentType == typeIndex) {
            T oldValue;
            memcpy(&oldValue, _data.data(), sizeof(T));
            if (memcmp(&oldValue, &value, sizeof(T)) == 0) {
                return true;  // No change
            }
        }

        _currentType = typeIndex;
        _data.fill(0);  // Clear data
        memcpy(_data.data(), &value, sizeof(T));
        notifyChange();
        return true;
    }

    /**
     * Set value by type name
     */
    template<typename T>
    bool set(const char* typeName, const T& value) {
        size_t index = findType(typeName);
        if (index == TypeCount) return false;
        return set<T>(static_cast<uint8_t>(index), value);
    }

    /**
     * Set raw data with type index
     */
    bool setRaw(uint8_t typeIndex, const void* data, size_t size) {
        if (readonly) return false;
        if (typeIndex >= TypeCount) return false;

        const VariantTypeDef& type = _types[typeIndex];
        if (size > type.size) return false;

        _currentType = typeIndex;
        _data.fill(0);
        memcpy(_data.data(), data, size);
        notifyChange();
        return true;
    }

    /**
     * Get raw value data pointer
     */
    const void* getValueData() const {
        return _data.data();
    }

    /**
     * Get current value size
     */
    size_t getValueSize() const {
        return _types[_currentType].size;
    }

    // =========== PropertyBase interface ===========

    uint8_t getTypeId() const override {
        return TYPE_VARIANT;
    }

    /**
     * Size on wire = 1 (type index) + current type's data size
     */
    size_t getSize() const override {
        return 1 + _types[_currentType].size;
    }

    /**
     * getData returns the raw data (without type index prefix)
     * Use getTypeIndex() separately to get the discriminant
     */
    const void* getData() const override {
        return _data.data();
    }

    /**
     * setData expects: u8 type_index + value bytes
     */
    void setData(const void* data, size_t size) override {
        if (readonly) return;
        if (size < 1) return;

        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        uint8_t newTypeIndex = bytes[0];

        if (newTypeIndex >= TypeCount) return;

        const VariantTypeDef& type = _types[newTypeIndex];
        size_t valueSize = size - 1;
        if (valueSize > type.size) valueSize = type.size;

        _currentType = newTypeIndex;
        _data.fill(0);
        if (valueSize > 0) {
            memcpy(_data.data(), bytes + 1, valueSize);
        }
        notifyChange();
    }

    /**
     * Get number of types (for schema encoding)
     */
    size_t getElementCount() const override {
        return TypeCount;
    }

    /**
     * Get type definitions for schema encoding
     */
    const VariantTypeDef* getVariantTypes() const {
        return _types.data();
    }

    /**
     * Validate value against current type's constraints
     */
    bool validateValue(const void* data, size_t size) const override {
        if (size < 1) return false;

        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        uint8_t typeIndex = bytes[0];

        if (typeIndex >= TypeCount) return false;

        const VariantTypeDef& type = _types[typeIndex];

        // Validate against constraints if present
        if (type.constraints.flags.any()) {
            const uint8_t* valueData = bytes + 1;

            switch (type.typeId) {
                case TYPE_BOOL:
                case TYPE_UINT8: {
                    if (size < 2) return false;
                    uint8_t val = valueData[0];
                    if (!type.constraints.validate(val)) return false;
                    break;
                }
                case TYPE_INT8: {
                    if (size < 2) return false;
                    int8_t val = static_cast<int8_t>(valueData[0]);
                    if (!type.constraints.validate(val)) return false;
                    break;
                }
                case TYPE_INT32: {
                    if (size < 5) return false;
                    int32_t val;
                    memcpy(&val, valueData, sizeof(int32_t));
                    if (!type.constraints.validate(val)) return false;
                    break;
                }
                case TYPE_FLOAT32: {
                    if (size < 5) return false;
                    float val;
                    memcpy(&val, valueData, sizeof(float));
                    if (!type.constraints.validate(val)) return false;
                    break;
                }
                default:
                    // Skip validation for complex types
                    break;
            }
        }

        return true;
    }

    // Schema encoding for VARIANT type
    bool encodeTypeDefinition(WriteBuffer& buf) const override {
        // VARIANT: type_id(0x23) + type_count + for each type: (utf8 name + DATA_TYPE_DEFINITION)
        if (!buf.writeByte(TYPE_VARIANT)) return false;
        if (buf.writeVarint(static_cast<uint32_t>(TypeCount)) == 0) return false;

        // For each possible type
        for (size_t i = 0; i < TypeCount; ++i) {
            const VariantTypeDef& type = _types[i];

            // Write name as utf8
            if (!buf.writeUtf8(type.name)) return false;

            // Write basic type definition
            if (!buf.writeByte(type.typeId)) return false;

            // Write constraints for basic types
            if (isBasicType(type.typeId)) {
                if (!type.constraints.flags.any()) {
                    if (!buf.writeByte(0)) return false;
                } else {
                    if (!buf.writeByte(type.constraints.flags.encode())) return false;
                    size_t typeSize = type.size;
                    if (type.constraints.flags.hasMin) {
                        if (!buf.writeBytes(type.constraints.minValue, typeSize)) return false;
                    }
                    if (type.constraints.flags.hasMax) {
                        if (!buf.writeBytes(type.constraints.maxValue, typeSize)) return false;
                    }
                    if (type.constraints.flags.hasStep) {
                        if (!buf.writeBytes(type.constraints.stepValue, typeSize)) return false;
                    }
                }
            }
        }

        return true;
    }

private:
    TypeArray _types;
    uint8_t _currentType;
    std::array<uint8_t, MaxDataSize> _data;
};

/**
 * Helper macros for defining variant types
 */
#define VARIANT_TYPE(name, type, size) \
    VariantTypeDef(name, type, size)

#define VARIANT_TYPE_C(name, type, size, constraints) \
    VariantTypeDef(name, type, size, constraints)

} // namespace MicroProto

#endif // MICROPROTO_VARIANT_PROPERTY_H