#ifndef MICROPROTO_PROPERTY_H
#define MICROPROTO_PROPERTY_H

#include "PropertyBase.h"
#include "TypeTraits.h"
#include <functional>

namespace MicroProto {

template<typename T>
class Property : public PropertyBase {
public:
    Property(
        const char* name,
        T defaultValue,
        PropertyLevel level,
        bool persistent = false,
        bool readonly = false,
        bool hidden = false,
        bool ble_exposed = false,
        uint8_t group_id = 0
    ) : PropertyBase(name, level, persistent, readonly, hidden, ble_exposed, group_id),
        value(defaultValue),
        defaultValue(defaultValue)
    {
    }

    // Read: implicit cast
    operator T() const {
        return value;
    }

    // Write: assignment operator
    Property& operator=(const T& newValue) {
        if (readonly) return *this;

        T oldValue = value;
        value = newValue;
        notifyChange(oldValue);

        return *this;
    }

    // Explicit get/set for clarity when needed
    T get() const {
        return value;
    }

    void set(const T& newValue) {
        *this = newValue;
    }

    // Reset to default
    void reset() {
        *this = defaultValue;
    }

    // PropertyBase interface implementation
    uint8_t getTypeId() const override {
        return TypeTraits<T>::type_id;
    }

    size_t getSize() const override {
        return sizeof(T);
    }

    const void* getData() const override {
        return &value;
    }

    void setData(const void* data, size_t size) override {
        if (size == sizeof(T)) {
            *this = *static_cast<const T*>(data);
        }
    }

    // Callback for onChange events
    using Callback = std::function<void(const T& oldVal, const T& newVal)>;

    void onChange(Callback cb) {
        userCallback = cb;
        setChangeCallback([](uint8_t) {}); // Enable notifications
    }

private:
    T value;
    T defaultValue;
    Callback userCallback = nullptr;

    void notifyChange(const T& oldValue) {
        PropertyBase::notifyChange();

        if (userCallback) {
            userCallback(oldValue, value);
        }
    }
};

// Convenience macros for property definition
#define PROPERTY_LOCAL(name, type, default_val, ...) \
    MicroProto::Property<type> name(#name, default_val, MicroProto::PropertyLevel::LOCAL, ##__VA_ARGS__)

#define PROPERTY_GROUP(name, type, default_val, group, ...) \
    MicroProto::Property<type> name(#name, default_val, MicroProto::PropertyLevel::GROUP, false, false, false, false, group, ##__VA_ARGS__)

#define PROPERTY_GLOBAL(name, type, default_val, ...) \
    MicroProto::Property<type> name(#name, default_val, MicroProto::PropertyLevel::GLOBAL, ##__VA_ARGS__)

} // namespace MicroProto

#endif // MICROPROTO_PROPERTY_H
