#ifndef MICROPROTO_FIELD_H
#define MICROPROTO_FIELD_H

#include "PropertyBase.h"  // For ValueConstraints
#include <type_traits>

namespace MicroProto {

/**
 * Value wrapper - transparent value access with runtime-modifiable constraints
 *
 * This wrapper is designed to be used in reflectable structs and containers.
 * It provides:
 * - Transparent access to the underlying value (acts like T)
 * - Runtime-modifiable constraints (min, max, step, readOnly, hidden)
 * - Automatic constraint application via compile-time defaults
 *
 * Usage:
 *   struct LedConfig {
 *       Value<uint8_t> brightness{128};  // No default constraints
 *       Value<uint8_t> speed{50};
 *       Value<bool> isOn{true};
 *       int32_t plainField;  // Plain fields also work
 *   };
 *
 *   config.brightness = 200;           // Transparent assignment
 *   uint8_t b = config.brightness;     // Transparent read
 *   config.brightness.setRange(0, 100); // Modify constraints at runtime
 *   config.brightness.constraints.readOnly = true;
 */
template<typename T>
struct Value {
    using value_type = T;

    T value{};
    ValueConstraints constraints{};

    // Flags that don't fit in ValueConstraints
    bool readOnly = false;
    bool hidden = false;

    // Default constructor
    Value() = default;

    // Value constructor
    Value(T v) : value(v) {}

    // Value + constraints constructor
    Value(T v, const ValueConstraints& c) : value(v), constraints(c) {}

    // Copy/move
    Value(const Value&) = default;
    Value(Value&&) = default;
    Value& operator=(const Value&) = default;
    Value& operator=(Value&&) = default;

    // Transparent value access via conversion
    operator T&() { return value; }
    operator const T&() const { return value; }

    // Assignment from value
    Value& operator=(const T& v) {
        if (!readOnly) {
            value = v;
        }
        return *this;
    }

    // Pointer-like access for struct types
    T* operator->() { return &value; }
    const T* operator->() const { return &value; }

    // Dereference operator
    T& operator*() { return value; }
    const T& operator*() const { return value; }

    // Explicit accessors
    T& get() { return value; }
    const T& get() const { return value; }

    // Comparison operators (compare the value)
    bool operator==(const T& other) const { return value == other; }
    bool operator!=(const T& other) const { return value != other; }
    bool operator<(const T& other) const { return value < other; }
    bool operator<=(const T& other) const { return value <= other; }
    bool operator>(const T& other) const { return value > other; }
    bool operator>=(const T& other) const { return value >= other; }

    // Arithmetic operators (return value, not Value)
    T operator+(const T& other) const { return value + other; }
    T operator-(const T& other) const { return value - other; }
    T operator*(const T& other) const { return value * other; }
    T operator/(const T& other) const { return value / other; }

    // Compound assignment
    Value& operator+=(const T& other) { if (!readOnly) value += other; return *this; }
    Value& operator-=(const T& other) { if (!readOnly) value -= other; return *this; }
    Value& operator*=(const T& other) { if (!readOnly) value *= other; return *this; }
    Value& operator/=(const T& other) { if (!readOnly) value /= other; return *this; }

    // =========== Fluent constraint setters ===========

    Value& setMin(T v) {
        constraints.setMin(v);
        return *this;
    }

    Value& setMax(T v) {
        constraints.setMax(v);
        return *this;
    }

    Value& setRange(T lo, T hi) {
        constraints.setMin(lo);
        constraints.setMax(hi);
        return *this;
    }

    Value& setStep(T v) {
        constraints.setStep(v);
        return *this;
    }

    Value& setReadOnly(bool v = true) {
        readOnly = v;
        return *this;
    }

    Value& setHidden(bool v = true) {
        hidden = v;
        return *this;
    }

    // =========== Validation ===========

    bool validate() const {
        return constraints.validate(value);
    }

    bool validate(const T& v) const {
        return constraints.validate(v);
    }

    // Set with validation - returns false if validation fails
    bool trySet(const T& v) {
        if (readOnly) return false;
        if (!validate(v)) return false;
        value = v;
        return true;
    }

    // Set with clamping to constraints
    void setClamp(T v) {
        if (readOnly) return;
        if (constraints.flags.hasMin) {
            T minVal = constraints.getMin<T>();
            if (v < minVal) v = minVal;
        }
        if (constraints.flags.hasMax) {
            T maxVal = constraints.getMax<T>();
            if (v > maxVal) v = maxVal;
        }
        value = v;
    }
};

/**
 * Type traits for Value detection
 */
template<typename T>
struct is_value : std::false_type {};

template<typename T>
struct is_value<Value<T>> : std::true_type {};

template<typename T>
inline constexpr bool is_value_v = is_value<T>::value;

/**
 * Extract underlying type from Value or pass through plain type
 */
template<typename T>
struct unwrap_value {
    using type = T;
};

template<typename T>
struct unwrap_value<Value<T>> {
    using type = T;
};

template<typename T>
using unwrap_value_t = typename unwrap_value<T>::type;

/**
 * Get value from Value wrapper or plain type
 */
template<typename T>
const auto& get_value(const T& v) {
    if constexpr (is_value_v<std::remove_cv_t<T>>) {
        return v.value;
    } else {
        return v;
    }
}

template<typename T>
auto& get_value(T& v) {
    if constexpr (is_value_v<std::remove_cv_t<T>>) {
        return v.value;
    } else {
        return v;
    }
}

/**
 * Get constraints pointer from Value, or nullptr for plain types
 */
template<typename T>
const ValueConstraints* get_constraints(const T&) {
    return nullptr;
}

template<typename T>
const ValueConstraints* get_constraints(const Value<T>& f) {
    return &f.constraints;
}

template<typename T>
ValueConstraints* get_constraints(T&) {
    return nullptr;
}

template<typename T>
ValueConstraints* get_constraints(Value<T>& f) {
    return &f.constraints;
}

/**
 * Check if value is read-only
 */
template<typename T>
bool is_readonly(const T&) {
    return false;
}

template<typename T>
bool is_readonly(const Value<T>& f) {
    return f.readOnly;
}

/**
 * Check if value is hidden
 */
template<typename T>
bool is_hidden(const T&) {
    return false;
}

template<typename T>
bool is_hidden(const Value<T>& f) {
    return f.hidden;
}

} // namespace MicroProto

#endif // MICROPROTO_FIELD_H
