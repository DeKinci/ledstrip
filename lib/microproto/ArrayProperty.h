#ifndef MICROPROTO_ARRAY_PROPERTY_H
#define MICROPROTO_ARRAY_PROPERTY_H

#include "PropertyBase.h"
#include "TypeTraits.h"
#include <array>

namespace MicroProto {

/**
 * ArrayProperty<T, N> - Fixed-size homogeneous array property
 *
 * Wire format: N packed elements (no count transmitted, schema defines size)
 * Schema format: TYPE_ARRAY + varint(N) + element_type_id + element_constraints
 *
 * Usage:
 *   ArrayProperty<uint8_t, 3> rgb("rgb", {255, 128, 0}, PropertyLevel::LOCAL,
 *       ArrayConstraints<uint8_t>().min(0).max(255));
 *
 *   ArrayProperty<int32_t, 2> position("position", {100, 200}, PropertyLevel::LOCAL);
 */
template<typename T, size_t N>
class ArrayProperty : public PropertyBase {
public:
    using ArrayType = std::array<T, N>;
    using Traits = TypeTraits<ArrayType>;

    // Constructor without constraints
    ArrayProperty(
        const char* name,
        const ArrayType& defaultValue,
        PropertyLevel level,
        const char* description = nullptr,
        UIHints uiHints = UIHints(),
        bool persistent = false,
        bool readonly = false,
        bool hidden = false,
        bool ble_exposed = false,
        uint8_t group_id = 0
    ) : PropertyBase(name, level, persistent, readonly, hidden, ble_exposed, group_id, description, uiHints),
        _value(defaultValue),
        _defaultValue(defaultValue)
    {
    }

    // Constructor with constraints
    ArrayProperty(
        const char* name,
        const ArrayType& defaultValue,
        PropertyLevel level,
        const ArrayConstraints<T>& constraints,
        const char* description = nullptr,
        UIHints uiHints = UIHints(),
        bool persistent = false,
        bool readonly = false,
        bool hidden = false,
        bool ble_exposed = false,
        uint8_t group_id = 0
    ) : PropertyBase(name, level, persistent, readonly, hidden, ble_exposed, group_id, description, uiHints),
        _value(defaultValue),
        _defaultValue(defaultValue),
        _elementConstraints(constraints.element)
    {
    }

    // Initializer list constructor without constraints
    ArrayProperty(
        const char* name,
        std::initializer_list<T> init,
        PropertyLevel level,
        const char* description = nullptr,
        UIHints uiHints = UIHints(),
        bool persistent = false,
        bool readonly = false,
        bool hidden = false,
        bool ble_exposed = false,
        uint8_t group_id = 0
    ) : PropertyBase(name, level, persistent, readonly, hidden, ble_exposed, group_id, description, uiHints)
    {
        size_t i = 0;
        for (auto it = init.begin(); it != init.end() && i < N; ++it, ++i) {
            _value[i] = *it;
            _defaultValue[i] = *it;
        }
        for (; i < N; ++i) {
            _value[i] = T{};
            _defaultValue[i] = T{};
        }
    }

    // Initializer list constructor with constraints
    ArrayProperty(
        const char* name,
        std::initializer_list<T> init,
        PropertyLevel level,
        const ArrayConstraints<T>& constraints,
        const char* description = nullptr,
        UIHints uiHints = UIHints(),
        bool persistent = false,
        bool readonly = false,
        bool hidden = false,
        bool ble_exposed = false,
        uint8_t group_id = 0
    ) : PropertyBase(name, level, persistent, readonly, hidden, ble_exposed, group_id, description, uiHints),
        _elementConstraints(constraints.element)
    {
        size_t i = 0;
        for (auto it = init.begin(); it != init.end() && i < N; ++it, ++i) {
            _value[i] = *it;
            _defaultValue[i] = *it;
        }
        for (; i < N; ++i) {
            _value[i] = T{};
            _defaultValue[i] = T{};
        }
    }

    // =========== Read operations ===========

    // Get entire array
    const ArrayType& get() const {
        return _value;
    }

    // Element access
    T operator[](size_t index) const {
        return (index < N) ? _value[index] : T{};
    }

    // =========== Write operations ===========

    // Set entire array
    ArrayProperty& operator=(const ArrayType& newValue) {
        if (readonly) return *this;
        if (_value == newValue) return *this;  // No change
        _value = newValue;
        notifyChange();
        return *this;
    }

    // Set single element
    void set(size_t index, const T& value) {
        if (readonly || index >= N) return;
        if (_value[index] == value) return;  // No change
        _value[index] = value;
        notifyChange();
    }

    // Set entire array
    void set(const ArrayType& newValue) {
        *this = newValue;
    }

    // Reset to default
    void reset() {
        if (readonly) return;
        if (_value == _defaultValue) return;  // No change
        _value = _defaultValue;
        notifyChange();
    }

    // Array size
    static constexpr size_t size() { return N; }

    // Iterator support
    auto begin() { return _value.begin(); }
    auto end() { return _value.end(); }
    auto begin() const { return _value.begin(); }
    auto end() const { return _value.end(); }

    // =========== PropertyBase interface ===========

    uint8_t getTypeId() const override {
        return TYPE_ARRAY;
    }

    size_t getSize() const override {
        return Traits::size;
    }

    const void* getData() const override {
        return _value.data();
    }

    void setData(const void* data, size_t size) override {
        if (readonly) return;
        if (size != Traits::size) return;

        // Check if data is different
        if (memcmp(_value.data(), data, size) == 0) return;  // No change

        memcpy(_value.data(), data, size);
        notifyChange();
    }

    // Container type metadata
    bool isContainer() const override { return true; }

    uint8_t getElementTypeId() const override {
        return TypeTraits<T>::type_id;
    }

    size_t getElementSize() const override {
        return TypeTraits<T>::size;
    }

    size_t getElementCount() const override {
        return N;
    }

    size_t getMaxElementCount() const override {
        return N;  // Fixed size for ARRAY
    }

    // Return element constraints if any are set
    const ValueConstraints* getElementConstraints() const override {
        return _elementConstraints.flags.any() ? &_elementConstraints : nullptr;
    }

    // Validate all elements against constraints
    bool validateValue(const void* data, size_t size) const override {
        if (size != Traits::size) return false;
        if (!_elementConstraints.flags.any()) return true;

        const T* elements = static_cast<const T*>(data);
        for (size_t i = 0; i < N; ++i) {
            if (!_elementConstraints.validate(elements[i])) return false;
        }
        return true;
    }

private:
    ArrayType _value;
    ArrayType _defaultValue;
    ValueConstraints _elementConstraints;
};

// Convenience type aliases
template<size_t N>
using ByteArray = ArrayProperty<uint8_t, N>;

using RGB = ArrayProperty<uint8_t, 3>;
using RGBA = ArrayProperty<uint8_t, 4>;

} // namespace MicroProto

#endif // MICROPROTO_ARRAY_PROPERTY_H
