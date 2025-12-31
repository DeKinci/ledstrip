#ifndef MICROPROTO_PROPERTY_H
#define MICROPROTO_PROPERTY_H

#include "PropertyBase.h"
#include "TypeTraits.h"
#include "MicroList.h"
#include "wire/TypeCodec.h"

namespace MicroProto {

/**
 * Property<T> - Single-value property with optional constraints
 *
 * Usage:
 *   Property<uint8_t> brightness("brightness", 128, PropertyLevel::LOCAL,
 *       Constraints<uint8_t>().min(0).max(255).step(1));
 *
 *   Property<int16_t> position("position", 0, PropertyLevel::LOCAL,
 *       Constraints<int16_t>().min(-1000).max(1000));
 *
 *   Property<bool> enabled("enabled", true, PropertyLevel::LOCAL);  // No constraints
 */
template<typename T>
class Property : public PropertyBase {
public:
    // Constructor without constraints
    Property(
        const char* name,
        T defaultValue,
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
    Property(
        const char* name,
        T defaultValue,
        PropertyLevel level,
        const Constraints<T>& constraints,
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
        _constraints(constraints.value)
    {
    }

    // =========== Read operations ===========

    // Implicit cast to T
    operator T() const {
        return _value;
    }

    // Explicit get
    T get() const {
        return _value;
    }

    // Get default value
    T getDefault() const {
        return _defaultValue;
    }

    // =========== Write operations ===========

    // Assignment operator
    Property& operator=(const T& newValue) {
        if (readonly) return *this;
        if (_value == newValue) return *this;  // No change

        // Validate against constraints (min/max/oneof)
        if (_constraints.flags.any() && !_constraints.validate(newValue)) {
            return *this;  // Reject invalid value
        }

        T oldValue = _value;
        _value = newValue;
        notifyChange(oldValue);

        return *this;
    }

    // Explicit set
    void set(const T& newValue) {
        *this = newValue;
    }

    // Try to set value, returns true if accepted (passes validation)
    bool trySet(const T& newValue) {
        if (readonly) return false;
        if (_constraints.flags.any() && !_constraints.validate(newValue)) {
            return false;
        }
        if (_value == newValue) return true;  // Already set, considered success
        *this = newValue;
        return true;
    }

    // Reset to default
    void reset() {
        *this = _defaultValue;
    }

    // =========== PropertyBase interface ===========

    uint8_t getTypeId() const override {
        return TypeTraits<T>::type_id;
    }

    size_t getSize() const override {
        return sizeof(T);
    }

    const void* getData() const override {
        return &_value;
    }

    void setData(const void* data, size_t size) override {
        if (size == sizeof(T)) {
            *this = *static_cast<const T*>(data);
        }
    }

    // Return constraints if any are set
    const ValueConstraints* getValueConstraints() const override {
        return _constraints.flags.any() ? &_constraints : nullptr;
    }

    // Validate incoming value against constraints
    bool validateValue(const void* data, size_t size) const override {
        if (size != sizeof(T)) return false;
        T val = *static_cast<const T*>(data);
        return _constraints.validate(val);
    }

    // Schema encoding using compile-time type info
    bool encodeTypeDefinition(WriteBuffer& buf) const override {
        return SchemaTypeEncoder::encode<T>(buf, getValueConstraints());
    }

    // =========== Change callback ===========

    // Per-property change callback with old/new values
    using TypedCallback = MicroFunction<void(T, T), sizeof(void*)>;

    void onChangeTyped(TypedCallback cb) {
        _typedCallback = cb;
    }

private:
    T _value;
    T _defaultValue;
    ValueConstraints _constraints;
    TypedCallback _typedCallback;

    void notifyChange(const T& oldValue) {
        PropertyBase::notifyChange();

        if (_typedCallback) {
            _typedCallback(oldValue, _value);
        }
    }
};

/**
 * Property<MicroList<T, Inline, Max>> - List property specialization
 *
 * Provides a variable-size list property backed by MicroList.
 * Uses LIST wire format: varint(count) + packed elements.
 *
 * Usage:
 *   Property<MicroList<uint8_t, 8, 32>> items("items", {1, 2, 3}, PropertyLevel::LOCAL);
 *   Property<MicroList<int32_t, 4, 16>> values("values", PropertyLevel::LOCAL);
 */
template<typename T, size_t InlineCapacity, size_t MaxCapacity>
class Property<MicroList<T, InlineCapacity, MaxCapacity>> : public PropertyBase {
public:
    using ListType = MicroList<T, InlineCapacity, MaxCapacity>;
    using Traits = TypeTraits<ListType>;

    // Constructor with empty list
    Property(
        const char* name,
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
    }

    // Constructor with initializer list
    Property(
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
    ) : PropertyBase(name, level, persistent, readonly, hidden, ble_exposed, group_id, description, uiHints),
        _value(init),
        _defaultValue(init)
    {
    }

    // Constructor with constraints
    Property(
        const char* name,
        std::initializer_list<T> init,
        PropertyLevel level,
        const ListConstraints<T>& constraints,
        const char* description = nullptr,
        UIHints uiHints = UIHints(),
        bool persistent = false,
        bool readonly = false,
        bool hidden = false,
        bool ble_exposed = false,
        uint8_t group_id = 0
    ) : PropertyBase(name, level, persistent, readonly, hidden, ble_exposed, group_id, description, uiHints),
        _value(init),
        _defaultValue(init),
        _containerConstraints(constraints.container),
        _elementConstraints(constraints.element)
    {
    }

    // =========== Read operations ===========

    // Get reference to the list
    const ListType& get() const { return _value; }
    ListType& get() { return _value; }

    // Element access
    const T& operator[](size_t index) const { return _value[index]; }
    T& operator[](size_t index) { return _value[index]; }

    // Size/capacity
    size_t size() const { return _value.size(); }
    size_t capacity() const { return _value.capacity(); }
    bool empty() const { return _value.empty(); }
    bool full() const { return _value.size() >= MaxCapacity; }

    // Iterators
    auto begin() { return _value.begin(); }
    auto end() { return _value.end(); }
    auto begin() const { return _value.begin(); }
    auto end() const { return _value.end(); }

    // Data pointer
    const T* data() const { return _value.data(); }

    // =========== Write operations ===========

    // Push element (validates against element constraints)
    bool push_back(const T& value) {
        if (readonly || _value.size() >= MaxCapacity) return false;
        // Validate element constraints
        if (_elementConstraints.flags.any() && !_elementConstraints.validate(value)) {
            return false;
        }
        _value.push_back(value);
        PropertyBase::notifyChange();
        return true;
    }

    // Pop element
    bool pop_back() {
        if (readonly || _value.empty()) return false;
        _value.pop_back();
        PropertyBase::notifyChange();
        return true;
    }

    // Clear list
    void clear() {
        if (readonly || _value.empty()) return;
        _value.clear();
        PropertyBase::notifyChange();
    }

    // Resize
    void resize(size_t newSize) {
        if (readonly) return;
        size_t clamped = (newSize > MaxCapacity) ? MaxCapacity : newSize;
        if (clamped == _value.size()) return;
        _value.resize(clamped);
        PropertyBase::notifyChange();
    }

    // Set element at index (validates against element constraints)
    bool set(size_t index, const T& value) {
        if (readonly || index >= _value.size()) return false;
        if (_value[index] == value) return true;  // No change, considered success
        // Validate element constraints
        if (_elementConstraints.flags.any() && !_elementConstraints.validate(value)) {
            return false;
        }
        _value[index] = value;
        PropertyBase::notifyChange();
        return true;
    }

    // Assign from another list (validates container and element constraints)
    Property& operator=(const ListType& newValue) {
        if (readonly) return *this;
        if (_value == newValue) return *this;

        // Validate container constraints
        if (_containerConstraints.any() && !_containerConstraints.validateLength(newValue.size())) {
            return *this;
        }

        // Validate element constraints
        if (_elementConstraints.flags.any()) {
            for (size_t i = 0; i < newValue.size(); ++i) {
                if (!_elementConstraints.validate(newValue[i])) {
                    return *this;
                }
            }
        }

        _value = newValue;
        PropertyBase::notifyChange();
        return *this;
    }

    // Reset to default
    void reset() {
        if (readonly) return;
        if (_value == _defaultValue) return;
        _value = _defaultValue;
        PropertyBase::notifyChange();
    }

    // =========== PropertyBase interface ===========

    uint8_t getTypeId() const override {
        return TYPE_LIST;
    }

    // Wire size = count * element_size (count is encoded separately as varint)
    size_t getSize() const override {
        return _value.size() * TypeTraits<T>::size;
    }

    const void* getData() const override {
        return _value.data();
    }

    void setData(const void* data, size_t size) override {
        if (readonly) return;

        size_t elementSize = TypeTraits<T>::size;
        size_t newCount = size / elementSize;
        if (newCount > MaxCapacity) newCount = MaxCapacity;

        // Update list from raw element data
        _value.clear();
        const T* elements = static_cast<const T*>(data);
        for (size_t i = 0; i < newCount; ++i) {
            _value.push_back(elements[i]);
        }
        PropertyBase::notifyChange();
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
        return _value.size();
    }

    size_t getMaxElementCount() const override {
        return MaxCapacity;
    }

    // Constraints
    const ContainerConstraints* getContainerConstraints() const override {
        return _containerConstraints.any() ? &_containerConstraints : nullptr;
    }

    const ValueConstraints* getElementConstraints() const override {
        return _elementConstraints.flags.any() ? &_elementConstraints : nullptr;
    }

    bool validateValue(const void* data, size_t size) const override {
        size_t elementSize = TypeTraits<T>::size;
        size_t newCount = size / elementSize;

        // Validate container constraints
        if (!_containerConstraints.validateLength(newCount)) return false;

        // Validate element constraints
        if (_elementConstraints.flags.any()) {
            const T* elements = static_cast<const T*>(data);
            for (size_t i = 0; i < newCount; ++i) {
                if (!_elementConstraints.validate(elements[i])) return false;
            }
        }

        return true;
    }

    // Schema encoding using compile-time type info
    bool encodeTypeDefinition(WriteBuffer& buf) const override {
        return SchemaTypeEncoder::encode<T, InlineCapacity, MaxCapacity>(
            buf, getElementConstraints(), getContainerConstraints());
    }

private:
    ListType _value;
    ListType _defaultValue;
    ContainerConstraints _containerConstraints;
    ValueConstraints _elementConstraints;
};

} // namespace MicroProto

#endif // MICROPROTO_PROPERTY_H
