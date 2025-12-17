#ifndef MICROPROTO_PROPERTY_H
#define MICROPROTO_PROPERTY_H

#include "PropertyBase.h"
#include "TypeTraits.h"

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

        T oldValue = _value;
        _value = newValue;
        notifyChange(oldValue);

        return *this;
    }

    // Explicit set
    void set(const T& newValue) {
        *this = newValue;
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

} // namespace MicroProto

#endif // MICROPROTO_PROPERTY_H
