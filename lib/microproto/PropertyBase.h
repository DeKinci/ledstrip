#ifndef MICROPROTO_PROPERTY_BASE_H
#define MICROPROTO_PROPERTY_BASE_H

#include <stdint.h>
#include <stddef.h>
#include <cstring>
#include <array>
#include <MicroFunction.h>

namespace MicroProto {

// Forward declaration for schema encoding
class WriteBuffer;

using microcore::MicroFunction;

enum class PropertyLevel : uint8_t {
    LOCAL = 0,
    GROUP = 1,
    GLOBAL = 2
};

/**
 * Predefined color palette for UI hints (pastel tones)
 * Index 0 = default/none, 1-12 = named colors
 */
enum class UIColor : uint8_t {
    NONE = 0,
    ROSE = 1,      // #fda4af - soft pink-red
    AMBER = 2,     // #fcd34d - warm yellow
    LIME = 3,      // #bef264 - fresh green
    CYAN = 4,      // #67e8f9 - light blue-green
    VIOLET = 5,    // #c4b5fd - soft purple
    PINK = 6,      // #f9a8d4 - bright pink
    TEAL = 7,      // #5eead4 - blue-green
    ORANGE = 8,    // #fdba74 - warm orange
    SKY = 9,       // #7dd3fc - light blue
    INDIGO = 10,   // #a5b4fc - blue-purple
    EMERALD = 11,  // #6ee7b7 - green
    SLATE = 12     // #cbd5e1 - neutral gray
};

/**
 * Widget hints by property type (wire value is uint8_t, 0=auto)
 * Note: Readonly is handled by the property's `readonly` flag, not widget type
 */
namespace Widget {
    // For BOOL type
    enum class Bool : uint8_t {
        AUTO = 0,
        TOGGLE = 1,
        CHECKBOX = 2
    };

    // For INT8, UINT8, INT32 types
    enum class Number : uint8_t {
        AUTO = 0,
        SLIDER = 1,
        SPINBOX = 2     // Number input box
    };

    // For FLOAT32 type
    enum class Decimal : uint8_t {
        AUTO = 0,
        SLIDER = 1,
        SPINBOX = 2     // Decimal input box
    };

    // For ARRAY<uint8_t, 3> or <uint8_t, 4> (RGB/RGBA)
    enum class Color : uint8_t {
        AUTO = 0,
        PICKER = 1,     // Color picker widget
        SLIDERS = 2,    // Individual R/G/B sliders
        HEX_CODE = 3    // Hex input #RRGGBB
    };

    // For LIST<uint8_t> (strings/text)
    enum class Text : uint8_t {
        AUTO = 0,
        LINE = 1,       // Single-line text input
        TEXTAREA = 2    // Multi-line text
    };

    // For ARRAY/LIST (generic)
    enum class Array : uint8_t {
        AUTO = 0,
        INLINE = 1,     // Inline editor [a, b, c]
        LIST = 2        // Vertical list editor
    };
}

/**
 * UI hints for property rendering
 *
 * Wire format (single byte):
 *   u8 ui_hints_flags {
 *       has_widget_hint: bit1  // bit 0
 *       has_unit: bit1         // bit 1
 *       has_icon: bit1         // bit 2
 *       reserved: bit1         // bit 3
 *       colorgroup: bit4       // bits 4-7 (0-15)
 *   }
 *   if has_widget: widget_id: u8
 *   if has_unit: unit_len: varint, unit: bytes[unit_len]
 *   if has_icon: icon_len: varint, icon: bytes[icon_len] (UTF-8 emoji)
 */
struct UIHints {
    UIColor color = UIColor::NONE;
    const char* unit = nullptr;   // Short unit text (e.g., "ms", "%", "°C")
    const char* icon = nullptr;   // Emoji icon (e.g., "💡", "🎨", "⚡")
    uint8_t widget = 0;           // Widget hint (0=auto, meaning depends on type)

    UIHints() = default;

    // Builder pattern
    UIHints& setColor(UIColor c) { color = c; return *this; }
    UIHints& setUnit(const char* u) { unit = u; return *this; }
    UIHints& setIcon(const char* i) { icon = i; return *this; }

    // Type-safe widget setters
    UIHints& setWidget(Widget::Bool w) { widget = static_cast<uint8_t>(w); return *this; }
    UIHints& setWidget(Widget::Number w) { widget = static_cast<uint8_t>(w); return *this; }
    UIHints& setWidget(Widget::Decimal w) { widget = static_cast<uint8_t>(w); return *this; }
    UIHints& setWidget(Widget::Color w) { widget = static_cast<uint8_t>(w); return *this; }
    UIHints& setWidget(Widget::Text w) { widget = static_cast<uint8_t>(w); return *this; }
    UIHints& setWidget(Widget::Array w) { widget = static_cast<uint8_t>(w); return *this; }
    UIHints& setWidget(uint8_t w) { widget = w; return *this; }

    bool hasUnit() const { return unit != nullptr && unit[0] != '\0'; }
    bool hasIcon() const { return icon != nullptr && icon[0] != '\0'; }
    bool hasWidget() const { return widget != 0; }
    bool any() const { return color != UIColor::NONE || hasUnit() || hasIcon() || hasWidget(); }

    // Encode flags with colorgroup in upper 4 bits
    uint8_t encodeFlags() const {
        uint8_t flags = (hasWidget() ? 0x01 : 0) | (hasUnit() ? 0x02 : 0) | (hasIcon() ? 0x04 : 0);
        uint8_t colorVal = static_cast<uint8_t>(color) & 0x0F;  // 4 bits max
        return flags | (colorVal << 4);
    }
};

// Convenience function to create UIHints inline
inline UIHints UI() { return UIHints(); }

/**
 * Validation flags for basic types (spec section 4.3)
 */
struct ValidationFlags {
    uint8_t hasMin : 1;
    uint8_t hasMax : 1;
    uint8_t hasStep : 1;
    uint8_t hasOneOf : 1;    // Enum values (future)
    uint8_t hasPattern : 1;  // Regex for strings (future)
    uint8_t reserved : 3;

    ValidationFlags() : hasMin(0), hasMax(0), hasStep(0), hasOneOf(0), hasPattern(0), reserved(0) {}

    uint8_t encode() const {
        return hasMin | (hasMax << 1) | (hasStep << 2) | (hasOneOf << 3) | (hasPattern << 4);
    }

    bool any() const { return hasMin || hasMax || hasStep || hasOneOf || hasPattern; }
};

/**
 * Length/order constraints for LIST/ARRAY types (spec section 4.3)
 */
struct ContainerConstraints {
    uint8_t hasMinLength : 1;
    uint8_t hasMaxLength : 1;
    uint8_t hasUnique : 1;        // Elements must be unique
    uint8_t isSorted : 1;         // Elements must be sorted ascending
    uint8_t isReverseSorted : 1;  // Elements must be sorted descending
    uint8_t reserved : 3;
    size_t minLength;
    size_t maxLength;

    ContainerConstraints() : hasMinLength(0), hasMaxLength(0), hasUnique(0),
                             isSorted(0), isReverseSorted(0), reserved(0),
                             minLength(0), maxLength(0) {}

    uint8_t encode() const {
        return hasMinLength | (hasMaxLength << 1) | (hasUnique << 2) |
               (isSorted << 3) | (isReverseSorted << 4);
    }

    bool any() const {
        return hasMinLength || hasMaxLength || hasUnique || isSorted || isReverseSorted;
    }

    bool validateLength(size_t len) const {
        if (hasMinLength && len < minLength) return false;
        if (hasMaxLength && len > maxLength) return false;
        return true;
    }
};

/**
 * Type-erased value constraints for basic types
 * Stores min/max/step as raw bytes (max 4 bytes for int32/float)
 * Supports oneof/enum validation with up to MAX_ONEOF_COUNT allowed values
 */
struct ValueConstraints {
    static constexpr size_t MAX_SIZE = 4;
    static constexpr size_t MAX_ONEOF_COUNT = 16;  // Max enum values

    ValidationFlags flags;
    uint8_t minValue[MAX_SIZE] = {};
    uint8_t maxValue[MAX_SIZE] = {};
    uint8_t stepValue[MAX_SIZE] = {};

    // Oneof (enum) values - stored as raw bytes, MAX_SIZE bytes per value
    uint8_t oneofValues[MAX_ONEOF_COUNT * MAX_SIZE] = {};
    uint8_t oneofCount = 0;
    uint8_t oneofValueSize = 0;  // Size of each value in bytes

    template<typename T>
    void setMin(T value) {
        static_assert(sizeof(T) <= MAX_SIZE, "Type too large");
        memcpy(minValue, &value, sizeof(T));
        flags.hasMin = true;
    }

    template<typename T>
    void setMax(T value) {
        static_assert(sizeof(T) <= MAX_SIZE, "Type too large");
        memcpy(maxValue, &value, sizeof(T));
        flags.hasMax = true;
    }

    template<typename T>
    void setStep(T value) {
        static_assert(sizeof(T) <= MAX_SIZE, "Type too large");
        memcpy(stepValue, &value, sizeof(T));
        flags.hasStep = true;
    }

    /**
     * Set allowed enum values (oneof constraint)
     * Usage: constraints.setOneOf<uint8_t>({1, 2, 4, 8});
     */
    template<typename T>
    void setOneOf(std::initializer_list<T> values) {
        static_assert(sizeof(T) <= MAX_SIZE, "Type too large");
        oneofCount = 0;
        oneofValueSize = sizeof(T);
        for (auto v : values) {
            if (oneofCount >= MAX_ONEOF_COUNT) break;
            memcpy(&oneofValues[oneofCount * MAX_SIZE], &v, sizeof(T));
            oneofCount++;
        }
        flags.hasOneOf = true;
    }

    /**
     * Add a single enum value
     */
    template<typename T>
    bool addOneOf(T value) {
        static_assert(sizeof(T) <= MAX_SIZE, "Type too large");
        if (oneofCount >= MAX_ONEOF_COUNT) return false;
        if (oneofCount == 0) {
            oneofValueSize = sizeof(T);
        }
        memcpy(&oneofValues[oneofCount * MAX_SIZE], &value, sizeof(T));
        oneofCount++;
        flags.hasOneOf = true;
        return true;
    }

    template<typename T>
    T getMin() const { T v; memcpy(&v, minValue, sizeof(T)); return v; }

    template<typename T>
    T getMax() const { T v; memcpy(&v, maxValue, sizeof(T)); return v; }

    template<typename T>
    T getStep() const { T v; memcpy(&v, stepValue, sizeof(T)); return v; }

    /**
     * Get oneof value by index
     */
    template<typename T>
    T getOneOf(size_t index) const {
        T v{};
        if (index < oneofCount) {
            memcpy(&v, &oneofValues[index * MAX_SIZE], sizeof(T));
        }
        return v;
    }

    /**
     * Check if value is in the oneof set
     */
    template<typename T>
    bool isInOneOf(T value) const {
        if (!flags.hasOneOf || oneofCount == 0) return true;
        for (size_t i = 0; i < oneofCount; i++) {
            T allowed;
            memcpy(&allowed, &oneofValues[i * MAX_SIZE], sizeof(T));
            if (value == allowed) return true;
        }
        return false;
    }

    template<typename T>
    bool validate(T value) const {
        if (flags.hasMin && value < getMin<T>()) return false;
        if (flags.hasMax && value > getMax<T>()) return false;
        if (flags.hasOneOf && !isInOneOf(value)) return false;
        return true;
    }
};

/**
 * Typed constraints builder for compile-time constraint specification
 *
 * Usage:
 *   Property<uint8_t> brightness("brightness", 128, PropertyLevel::LOCAL,
 *       Constraints<uint8_t>().min(0).max(255).step(1));
 *
 *   Property<uint8_t> mode("mode", 0, PropertyLevel::LOCAL,
 *       Constraints<uint8_t>().oneof({0, 1, 2, 4}));  // Enum values
 */
template<typename T>
struct Constraints {
    ValueConstraints value;

    constexpr Constraints() = default;

    constexpr Constraints& min(T v) {
        value.setMin(v);
        return *this;
    }

    constexpr Constraints& max(T v) {
        value.setMax(v);
        return *this;
    }

    constexpr Constraints& step(T v) {
        value.setStep(v);
        return *this;
    }

    /**
     * Set allowed enum values (oneof constraint)
     * Value must be one of the specified values to be valid
     */
    Constraints& oneof(std::initializer_list<T> values) {
        value.setOneOf<T>(values);
        return *this;
    }
};

/**
 * Container constraints builder for LIST types
 *
 * Usage:
 *   ListProperty<uint8_t, 64> name("name", "ESP32", PropertyLevel::LOCAL,
 *       ListConstraints<uint8_t>().minLength(1).maxLength(32));
 */
template<typename T>
struct ListConstraints {
    ContainerConstraints container;
    ValueConstraints element;

    constexpr ListConstraints() = default;

    // Container constraints
    constexpr ListConstraints& minLength(size_t len) {
        container.minLength = len;
        container.hasMinLength = true;
        return *this;
    }

    constexpr ListConstraints& maxLength(size_t len) {
        container.maxLength = len;
        container.hasMaxLength = true;
        return *this;
    }

    constexpr ListConstraints& unique() {
        container.hasUnique = true;
        return *this;
    }

    constexpr ListConstraints& sorted() {
        container.isSorted = true;
        container.isReverseSorted = false;
        return *this;
    }

    constexpr ListConstraints& reverseSorted() {
        container.isReverseSorted = true;
        container.isSorted = false;
        return *this;
    }

    // Element constraints
    constexpr ListConstraints& elementMin(T v) {
        element.setMin(v);
        return *this;
    }

    constexpr ListConstraints& elementMax(T v) {
        element.setMax(v);
        return *this;
    }

    constexpr ListConstraints& elementStep(T v) {
        element.setStep(v);
        return *this;
    }
};

/**
 * Element constraints builder for ARRAY types
 *
 * Usage:
 *   ArrayProperty<uint8_t, 3> rgb("rgb", {255, 128, 0}, PropertyLevel::LOCAL,
 *       ArrayConstraints<uint8_t>().min(0).max(255));
 */
template<typename T>
struct ArrayConstraints {
    ValueConstraints element;

    constexpr ArrayConstraints() = default;

    constexpr ArrayConstraints& min(T v) {
        element.setMin(v);
        return *this;
    }

    constexpr ArrayConstraints& max(T v) {
        element.setMax(v);
        return *this;
    }

    constexpr ArrayConstraints& step(T v) {
        element.setStep(v);
        return *this;
    }
};

class PropertyBase {
public:
    const uint8_t id;
    const char* name;
    const char* description;  // Human-readable description (nullptr if none)
    const PropertyLevel level;
    const bool persistent;
    const bool readonly;
    const bool hidden;
    const bool ble_exposed;
    const uint8_t group_id;
    const UIHints ui;  // UI rendering hints (color, unit, icon, widget)

    // Property registry - O(1) lookup by ID
    static constexpr size_t MAX_PROPERTIES = 256;
    static std::array<PropertyBase*, MAX_PROPERTIES> byId;
    static uint8_t count;  // Number of registered properties

    // Fast lookup - returns nullptr if ID not found
    static PropertyBase* find(uint8_t id) {
        return byId[id];
    }

    PropertyBase(
        const char* name,
        PropertyLevel level,
        bool persistent = false,
        bool readonly = false,
        bool hidden = false,
        bool ble_exposed = false,
        uint8_t group_id = 0,
        const char* description = nullptr,
        UIHints uiHints = UIHints()
    );

    virtual ~PropertyBase() = default;

    // Virtual interface for type-erased operations
    virtual uint8_t getTypeId() const = 0;
    virtual size_t getSize() const = 0;
    virtual const void* getData() const = 0;
    virtual void setData(const void* data, size_t size) = 0;

    // Container type metadata (overridden by ArrayProperty/ListProperty)
    virtual bool isContainer() const { return false; }
    virtual uint8_t getElementTypeId() const { return 0; }
    virtual size_t getElementSize() const { return 0; }
    virtual size_t getElementCount() const { return 0; }  // ARRAY: fixed count, LIST: current count
    virtual size_t getMaxElementCount() const { return 0; }  // LIST: max capacity

    // Constraint access (overridden by Property/ArrayProperty/ListProperty)
    virtual const ValueConstraints* getValueConstraints() const { return nullptr; }
    virtual const ValueConstraints* getElementConstraints() const { return nullptr; }
    virtual const ContainerConstraints* getContainerConstraints() const { return nullptr; }

    // Validation - returns true if value passes all constraints
    // Default implementation returns true; derived classes override
    virtual bool validateValue(const void* data, size_t size) const { return true; }

    // Schema encoding - encode DATA_TYPE_DEFINITION for this property's type
    // Uses compile-time type info via template visitor pattern
    virtual bool encodeTypeDefinition(WriteBuffer& buf) const = 0;

    // Persistence - virtual methods for NVS storage
    // Default implementation uses PropertyStorage::save/load with getData()/setData()
    // ResourceProperty overrides these to serialize headers properly
    virtual bool saveToNVS();
    virtual bool loadFromNVS();

    // Per-property change callback - called immediately on value change
    // Simple function pointer (no captures needed for app-level reactions)
    using ChangeCallback = MicroFunction<void(), 0>;

    // Set the change callback (replaces any existing)
    void onChange(ChangeCallback callback) { _onChange = callback; }

    // Clear the change callback
    void clearOnChange() { _onChange.clear(); }

protected:
    void notifyChange();

private:
    static uint8_t nextId;
    ChangeCallback _onChange;
};

} // namespace MicroProto

#endif // MICROPROTO_PROPERTY_BASE_H
