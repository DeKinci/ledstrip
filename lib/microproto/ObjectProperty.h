#ifndef MICROPROTO_OBJECT_PROPERTY_H
#define MICROPROTO_OBJECT_PROPERTY_H

#include "PropertyBase.h"
#include "TypeTraits.h"
#include "Reflect.h"
#include "Field.h"
#include "MicroList.h"
#include "wire/TypeCodec.h"
#include <array>
#include <vector>
#include <string.h>

namespace MicroProto {

/**
 * ObjectFieldDef - Describes a single field within an OBJECT property
 * Used for schema encoding and wire format.
 */
struct ObjectFieldDef {
    const char* name;           // Field name (from reflection or explicit)
    uint8_t typeId;             // TYPE_BOOL, TYPE_UINT8, etc.
    uint16_t offset;            // Offset within struct
    uint16_t size;              // Size of this field in bytes
    const ValueConstraints* constraints;  // Pointer to Field's constraints (may be null)

    ObjectFieldDef()
        : name(nullptr), typeId(0), offset(0), size(0), constraints(nullptr) {}

    ObjectFieldDef(const char* n, uint8_t type, uint16_t off, uint16_t sz,
                   const ValueConstraints* c = nullptr)
        : name(n), typeId(type), offset(off), size(sz), constraints(c) {}
};

/**
 * Type ID mapping for common types
 * Maps C++ types to MicroProto spec type IDs.
 * Note: int16/uint16/uint32 map to TYPE_INT32 as spec only defines INT32 for integers > 8 bits
 */
template<typename T> struct TypeIdOf { static constexpr uint8_t value = TYPE_OBJECT; };
template<> struct TypeIdOf<bool> { static constexpr uint8_t value = TYPE_BOOL; };
template<> struct TypeIdOf<uint8_t> { static constexpr uint8_t value = TYPE_UINT8; };
template<> struct TypeIdOf<int8_t> { static constexpr uint8_t value = TYPE_INT8; };
template<> struct TypeIdOf<uint16_t> { static constexpr uint8_t value = TYPE_INT32; };  // No TYPE_UINT16 in spec
template<> struct TypeIdOf<int16_t> { static constexpr uint8_t value = TYPE_INT32; };   // No TYPE_INT16 in spec
template<> struct TypeIdOf<uint32_t> { static constexpr uint8_t value = TYPE_INT32; };  // No TYPE_UINT32 in spec
template<> struct TypeIdOf<int32_t> { static constexpr uint8_t value = TYPE_INT32; };
template<> struct TypeIdOf<float> { static constexpr uint8_t value = TYPE_FLOAT32; };

// Value<T> maps to T's type ID
template<typename T> struct TypeIdOf<Value<T>> { static constexpr uint8_t value = TypeIdOf<T>::value; };

// std::array maps to TYPE_ARRAY
template<typename T, size_t N> struct TypeIdOf<std::array<T, N>> { static constexpr uint8_t value = TYPE_ARRAY; };

// std::vector maps to TYPE_LIST
// Note: Wire encoding for vectors requires special handling (not just memcpy)
template<typename T> struct TypeIdOf<std::vector<T>> { static constexpr uint8_t value = TYPE_LIST; };

// ============================================================================
// Wire-safe type checking
// Types that can be safely serialized via memcpy (no heap pointers)
// ============================================================================

namespace detail {

// Forward declaration for recursive check
template<typename T, typename = void>
struct is_wire_safe_impl : std::false_type {};

// Basic types are wire-safe
template<> struct is_wire_safe_impl<bool> : std::true_type {};
template<> struct is_wire_safe_impl<int8_t> : std::true_type {};
template<> struct is_wire_safe_impl<uint8_t> : std::true_type {};
template<> struct is_wire_safe_impl<int16_t> : std::true_type {};
template<> struct is_wire_safe_impl<uint16_t> : std::true_type {};
template<> struct is_wire_safe_impl<int32_t> : std::true_type {};
template<> struct is_wire_safe_impl<uint32_t> : std::true_type {};
template<> struct is_wire_safe_impl<float> : std::true_type {};
template<> struct is_wire_safe_impl<double> : std::true_type {};

// Helper to detect std::array
template<typename T> struct is_std_array : std::false_type {};
template<typename T, size_t N> struct is_std_array<std::array<T, N>> : std::true_type {};

// std::array is wire-safe if element type is wire-safe
template<typename T, size_t N>
struct is_wire_safe_impl<std::array<T, N>> : is_wire_safe_impl<T> {};

// Value<T> is wire-safe if T is wire-safe
template<typename T>
struct is_wire_safe_impl<Value<T>> : is_wire_safe_impl<T> {};

// std::vector is NOT wire-safe (heap allocated)
template<typename T>
struct is_wire_safe_impl<std::vector<T>> : std::false_type {};

// MicroList is NOT wire-safe via memcpy (may have heap pointer)
// Use MicroList with ListProperty instead, which handles serialization properly
template<typename T, size_t Inline, size_t Max>
struct is_wire_safe_impl<MicroList<T, Inline, Max>> : std::false_type {};

// Aggregate structs: check all fields recursively
// Exclude std::array (handled above) to avoid ambiguity
template<typename T>
struct is_wire_safe_impl<T, std::enable_if_t<
    std::is_aggregate_v<T> &&
    !std::is_array_v<T> &&
    !is_std_array<T>::value &&
    reflect::field_count_v<T> >= 1
>> {
    // Helper to check all fields
    template<typename U, size_t... Is>
    static constexpr bool check_fields(std::index_sequence<Is...>) {
        return (is_wire_safe_impl<
            std::remove_cv_t<std::remove_reference_t<
                reflect::field_type_t<U, Is>
            >>
        >::value && ...);
    }

    static constexpr bool value = check_fields<T>(
        std::make_index_sequence<reflect::field_count_v<T>>{}
    );
};

} // namespace detail

/**
 * Check if a type can be safely serialized via memcpy
 * Returns false for types with heap allocations (vector, string, pointers)
 */
template<typename T>
inline constexpr bool is_wire_safe_v = detail::is_wire_safe_impl<std::remove_cv_t<T>>::value;

/**
 * ObjectProperty - Reflection-based heterogeneous structure property
 *
 * OBJECT (0x22) stores a C++ struct as a MicroProto property.
 * Field names, types, and offsets are derived automatically via reflection.
 *
 * Template parameter:
 *   T - A reflectable aggregate struct type
 *
 * Usage:
 *   struct Position {
 *       int32_t x;
 *       int32_t y;
 *       int32_t z;
 *   };
 *
 *   ObjectProperty<Position> position("position");
 *   position->x = 100;
 *   position->y = 200;
 *   int32_t z = position->z;
 *
 * With Value wrappers for constraints:
 *   struct LedConfig {
 *       Value<uint8_t> brightness{128};
 *       Value<uint8_t> speed{50};
 *       Value<bool> enabled{true};
 *   };
 *
 *   ObjectProperty<LedConfig> config("config");
 *   config->brightness.setRange(0, 255);
 *   config->brightness = 200;
 */
template<typename T>
class ObjectProperty : public PropertyBase {
public:
    static_assert(reflect::is_reflectable_v<T>, "ObjectProperty requires a reflectable aggregate struct");
    static_assert(is_wire_safe_v<T>,
        "ObjectProperty requires wire-safe types only (no std::vector, std::string, or pointers)");

    static constexpr size_t FieldCount = reflect::field_count_v<T>;
    static constexpr size_t DataSize = sizeof(T);

    /**
     * Constructor with property name
     */
    ObjectProperty(
        const char* name,
        PropertyLevel level = PropertyLevel::LOCAL,
        const char* description = nullptr,
        UIHints uiHints = UIHints(),
        bool persistent = false,
        bool readonly = false,
        bool hidden = false,
        bool ble_exposed = false,
        uint8_t group_id = 0
    ) : PropertyBase(name, level, persistent, readonly, hidden, ble_exposed, group_id, description, uiHints),
        _data{}
    {
    }

    /**
     * Constructor with initial value
     */
    ObjectProperty(
        const char* name,
        const T& initialValue,
        PropertyLevel level = PropertyLevel::LOCAL,
        const char* description = nullptr,
        UIHints uiHints = UIHints(),
        bool persistent = false,
        bool readonly = false,
        bool hidden = false,
        bool ble_exposed = false,
        uint8_t group_id = 0
    ) : PropertyBase(name, level, persistent, readonly, hidden, ble_exposed, group_id, description, uiHints),
        _data(initialValue)
    {
    }

    // =========== Direct struct access ===========

    /**
     * Get reference to the underlying struct
     */
    T& get() { return _data; }
    const T& get() const { return _data; }

    /**
     * Pointer-like access to struct members
     */
    T* operator->() { return &_data; }
    const T* operator->() const { return &_data; }

    /**
     * Dereference to get struct reference
     */
    T& operator*() { return _data; }
    const T& operator*() const { return _data; }

    /**
     * Assignment from struct value
     */
    ObjectProperty& operator=(const T& value) {
        if (!readonly) {
            _data = value;
            notifyChange();
        }
        return *this;
    }

    // =========== Field access by index ===========

    /**
     * Get the number of fields
     */
    constexpr size_t fieldCount() const {
        return FieldCount;
    }

    /**
     * Get typed field value by index
     */
    template<size_t N>
    auto& getField() {
        return reflect::get<N>(_data);
    }

    template<size_t N>
    const auto& getField() const {
        return reflect::get<N>(_data);
    }

    /**
     * Iterate over all fields
     */
    template<typename F>
    void forEachField(F&& callback) {
        reflect::for_each_field(_data, std::forward<F>(callback));
    }

    template<typename F>
    void forEachField(F&& callback) const {
        reflect::for_each_field(const_cast<T&>(_data), std::forward<F>(callback));
    }

    // =========== PropertyBase interface ===========

    uint8_t getTypeId() const override {
        return TYPE_OBJECT;
    }

    size_t getSize() const override {
        return DataSize;
    }

    const void* getData() const override {
        return &_data;
    }

    void setData(const void* data, size_t size) override {
        if (readonly) return;
        if (size > DataSize) size = DataSize;
        memcpy(&_data, data, size);
        notifyChange();
    }

    size_t getElementCount() const override {
        return FieldCount;
    }

    /**
     * Build field definitions for schema encoding
     * Note: This builds the array dynamically - for efficiency, cache if needed
     */
    template<typename Callback>
    void getFieldDefs(Callback&& callback) const {
        buildFieldDefs([&callback](size_t, const ObjectFieldDef& def) {
            callback(def);
            return true;
        });
    }

    /**
     * Validate all field values against their constraints
     */
    bool validateValue(const void* data, size_t size) const override {
        if (size != DataSize) return false;

        // Copy to temporary to validate
        T temp;
        memcpy(&temp, data, size);

        bool valid = true;
        reflect::for_each_field(temp, [&valid](auto, auto& field) {
            if constexpr (is_value_v<std::remove_reference_t<decltype(field)>>) {
                if (!field.validate()) {
                    valid = false;
                }
            }
        });
        return valid;
    }

    /**
     * Mark as changed (call after modifying via ->)
     */
    void markChanged() {
        notifyChange();
    }

    // Schema encoding - encodes as OBJECT with field definitions
    // For full struct support, we'd need reflection to get field names
    // Currently encodes as opaque OBJECT (field_count=0)
    bool encodeTypeDefinition(WriteBuffer& buf) const override {
        // Use struct encoder - encodes as OBJECT with field_count=0
        return SchemaTypeEncoder::encode<T>(buf, nullptr);
    }

private:
    T _data;

    /**
     * Build ObjectFieldDef for each field using reflection
     */
    template<typename Callback>
    void buildFieldDefs(Callback&& callback) const {
        size_t idx = 0;
        reflect::for_each_field(const_cast<T&>(_data), [&](auto I, auto& field) {
            using FieldType = std::remove_reference_t<decltype(field)>;
            using ValueType = unwrap_value_t<FieldType>;

            // Get field offset by pointer arithmetic
            const uint8_t* base = reinterpret_cast<const uint8_t*>(&_data);
            const uint8_t* fieldPtr = reinterpret_cast<const uint8_t*>(&field);
            uint16_t offset = static_cast<uint16_t>(fieldPtr - base);

            // Get constraints if Value wrapper
            const ValueConstraints* constraints = nullptr;
            if constexpr (is_value_v<FieldType>) {
                constraints = &field.constraints;
            }

            ObjectFieldDef def(
                nullptr,  // Name requires member pointer - set separately
                TypeIdOf<ValueType>::value,
                offset,
                sizeof(ValueType),
                constraints
            );

            callback(idx++, def);
        });
    }
};

/**
 * Helper macro to create ObjectProperty with field name registration
 *
 * Note: For automatic field names, use OBJECT_PROPERTY_NAMED macro
 * which registers member pointers for name extraction.
 */
#define OBJECT_PROPERTY(Type, name, ...) \
    ObjectProperty<Type> name{#name, ##__VA_ARGS__}

} // namespace MicroProto

#endif // MICROPROTO_OBJECT_PROPERTY_H