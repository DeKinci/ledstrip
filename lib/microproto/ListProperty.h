#ifndef MICROPROTO_LIST_PROPERTY_H
#define MICROPROTO_LIST_PROPERTY_H

#include "PropertyBase.h"
#include "TypeTraits.h"
#include <array>
#include <string.h>

namespace MicroProto {

/**
 * ListProperty<T, MaxN> - Variable-size homogeneous list property
 *
 * Wire format: varint(count) + count packed elements
 * Schema format: TYPE_LIST + element_type_id + container_constraints + element_constraints
 *
 * Uses fixed storage (std::array) but tracks actual count.
 * MaxN defines the maximum capacity for this property (compile-time limit).
 *
 * Usage:
 *   ListProperty<uint8_t, 64> deviceName("name", "ESP32", PropertyLevel::LOCAL,
 *       ListConstraints<uint8_t>().minLength(1).maxLength(32));
 *
 *   ListProperty<int16_t, 100> samples("samples", PropertyLevel::LOCAL,
 *       ListConstraints<int16_t>().minLength(10).maxLength(100).elementMin(-1000).elementMax(1000));
 *
 *   ListProperty<uint8_t, 8> pattern("pattern", {10, 20, 30}, PropertyLevel::LOCAL);
 */
template<typename T, size_t MaxN>
class ListProperty : public PropertyBase {
public:
    using ElementType = T;
    static constexpr size_t MaxSize = MaxN;

    // Empty list constructor
    ListProperty(
        const char* name,
        PropertyLevel level,
        const char* description = nullptr,
        UIHints uiHints = UIHints(),
        bool persistent = false,
        bool readonly = false,
        bool hidden = false,
        bool ble_exposed = false,
        uint8_t group_id = 0
    ) : PropertyBase(name, level, persistent, readonly, hidden, ble_exposed, group_id, description, uiHints),
        _count(0)
    {
        _storage.fill(T{});
    }

    // Empty list constructor with constraints
    ListProperty(
        const char* name,
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
        _count(0),
        _containerConstraints(constraints.container),
        _elementConstraints(constraints.element)
    {
        _storage.fill(T{});
    }

    // Constructor with initial values
    ListProperty(
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
        _count(0)
    {
        _storage.fill(T{});
        for (auto it = init.begin(); it != init.end() && _count < MaxN; ++it) {
            _storage[_count++] = *it;
        }
    }

    // Constructor with initial values and constraints
    ListProperty(
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
        _count(0),
        _containerConstraints(constraints.container),
        _elementConstraints(constraints.element)
    {
        _storage.fill(T{});
        for (auto it = init.begin(); it != init.end() && _count < MaxN; ++it) {
            _storage[_count++] = *it;
        }
    }

    // String constructor for ListProperty<uint8_t, N> (string type)
    template<typename U = T, typename = typename std::enable_if<std::is_same<U, uint8_t>::value>::type>
    ListProperty(
        const char* name,
        const char* str,
        PropertyLevel level,
        const char* description = nullptr,
        UIHints uiHints = UIHints(),
        bool persistent = false,
        bool readonly = false,
        bool hidden = false,
        bool ble_exposed = false,
        uint8_t group_id = 0
    ) : PropertyBase(name, level, persistent, readonly, hidden, ble_exposed, group_id, description, uiHints),
        _count(0)
    {
        _storage.fill(T{});
        setString(str);
    }

    // String constructor with constraints
    template<typename U = T, typename = typename std::enable_if<std::is_same<U, uint8_t>::value>::type>
    ListProperty(
        const char* name,
        const char* str,
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
        _count(0),
        _containerConstraints(constraints.container),
        _elementConstraints(constraints.element)
    {
        _storage.fill(T{});
        setString(str);
    }

    // Current element count
    size_t count() const { return _count; }

    // Maximum capacity
    static constexpr size_t capacity() { return MaxN; }

    // Check if empty
    bool empty() const { return _count == 0; }

    // Check if full
    bool full() const { return _count >= MaxN; }

    // Element access (bounds checked)
    T operator[](size_t index) const {
        return (index < _count) ? _storage[index] : T{};
    }

    // Get pointer to data
    const T* data() const { return _storage.data(); }

    // Set single element
    void set(size_t index, const T& value) {
        if (readonly || index >= _count) return;
        if (_storage[index] == value) return;  // No change
        _storage[index] = value;
        notifyChange();
    }

    // Add element to end
    bool push(const T& value) {
        if (readonly || _count >= MaxN) return false;
        _storage[_count++] = value;
        notifyChange();
        return true;
    }

    // Remove last element
    bool pop() {
        if (readonly || _count == 0) return false;
        _count--;
        notifyChange();
        return true;
    }

    // Clear all elements
    void clear() {
        if (readonly) return;
        if (_count == 0) return;  // No change
        _count = 0;
        notifyChange();
    }

    // Resize (truncate or extend with default values)
    void resize(size_t newCount) {
        if (readonly) return;
        if (newCount > MaxN) newCount = MaxN;
        if (newCount == _count) return;  // No change
        // Fill new elements with default value
        for (size_t i = _count; i < newCount; ++i) {
            _storage[i] = T{};
        }
        _count = newCount;
        notifyChange();
    }

    // Set from array
    template<size_t N>
    void setFrom(const std::array<T, N>& arr) {
        if (readonly) return;
        size_t newCount = (N < MaxN) ? N : MaxN;

        // Check if anything is different
        bool changed = (newCount != _count);
        if (!changed) {
            for (size_t i = 0; i < newCount; ++i) {
                if (_storage[i] != arr[i]) {
                    changed = true;
                    break;
                }
            }
        }
        if (!changed) return;

        _count = newCount;
        for (size_t i = 0; i < _count; ++i) {
            _storage[i] = arr[i];
        }
        notifyChange();
    }

    // Set from pointer + count
    void setFrom(const T* data, size_t count) {
        if (readonly) return;
        size_t newCount = (count < MaxN) ? count : MaxN;

        // Check if anything is different
        bool changed = (newCount != _count);
        if (!changed) {
            for (size_t i = 0; i < newCount; ++i) {
                if (_storage[i] != data[i]) {
                    changed = true;
                    break;
                }
            }
        }
        if (!changed) return;

        _count = newCount;
        for (size_t i = 0; i < _count; ++i) {
            _storage[i] = data[i];
        }
        notifyChange();
    }

    // String operations (for ListProperty<uint8_t, N>)
    template<typename U = T>
    typename std::enable_if<std::is_same<U, uint8_t>::value, void>::type
    setString(const char* str) {
        if (readonly) return;

        // Calculate new length
        size_t newCount = 0;
        const char* p = str;
        while (*p && newCount < MaxN) {
            newCount++;
            p++;
        }

        // Check if anything is different
        bool changed = (newCount != _count);
        if (!changed) {
            for (size_t i = 0; i < newCount; ++i) {
                if (_storage[i] != static_cast<uint8_t>(str[i])) {
                    changed = true;
                    break;
                }
            }
        }
        if (!changed) return;

        _count = 0;
        while (*str && _count < MaxN) {
            _storage[_count++] = static_cast<uint8_t>(*str++);
        }
        notifyChange();
    }

    // Get as null-terminated string (for ListProperty<uint8_t, N>)
    // Buffer must have space for count+1 bytes
    template<typename U = T>
    typename std::enable_if<std::is_same<U, uint8_t>::value, size_t>::type
    getString(char* buf, size_t bufSize) const {
        size_t copyLen = (_count < bufSize - 1) ? _count : bufSize - 1;
        for (size_t i = 0; i < copyLen; ++i) {
            buf[i] = static_cast<char>(_storage[i]);
        }
        buf[copyLen] = '\0';
        return copyLen;
    }

    // Iterator support
    auto begin() { return _storage.begin(); }
    auto end() { return _storage.begin() + _count; }
    auto begin() const { return _storage.begin(); }
    auto end() const { return _storage.begin() + _count; }

    // PropertyBase interface
    uint8_t getTypeId() const override {
        return TYPE_LIST;
    }

    // Wire size = varint(count) + count * element_size
    // For getData(), we return raw element data (count handled separately)
    size_t getSize() const override {
        return _count * TypeTraits<T>::size;
    }

    const void* getData() const override {
        return _storage.data();
    }

    void setData(const void* data, size_t size) override {
        if (readonly) return;

        size_t elementSize = TypeTraits<T>::size;
        size_t newCount = size / elementSize;
        if (newCount > MaxN) newCount = MaxN;

        // Check if anything is different
        size_t newSize = newCount * elementSize;
        bool changed = (newCount != _count) ||
                       (memcmp(_storage.data(), data, newSize) != 0);
        if (!changed) return;

        memcpy(_storage.data(), data, newSize);
        _count = newCount;
        notifyChange();
    }

    // Container type metadata (PropertyBase virtual interface)
    bool isContainer() const override { return true; }

    uint8_t getElementTypeId() const override {
        return TypeTraits<T>::type_id;
    }

    size_t getElementSize() const override {
        return TypeTraits<T>::size;
    }

    size_t getElementCount() const override {
        return _count;  // Current count for LIST
    }

    size_t getMaxElementCount() const override {
        return MaxN;  // Max capacity
    }

    // Additional LIST-specific methods
    size_t getCount() const {
        return _count;
    }

    size_t getMaxCount() const {
        return MaxN;
    }

    // Return container constraints if any are set
    const ContainerConstraints* getContainerConstraints() const override {
        return _containerConstraints.any() ? &_containerConstraints : nullptr;
    }

    // Return element constraints if any are set
    const ValueConstraints* getElementConstraints() const override {
        return _elementConstraints.flags.any() ? &_elementConstraints : nullptr;
    }

    // Validate incoming data against all constraints
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

        // Validate unique constraint
        if (_containerConstraints.hasUnique) {
            const T* elements = static_cast<const T*>(data);
            for (size_t i = 0; i < newCount; ++i) {
                for (size_t j = i + 1; j < newCount; ++j) {
                    if (elements[i] == elements[j]) return false;
                }
            }
        }

        // Validate sorted constraint
        if (_containerConstraints.isSorted && newCount > 1) {
            const T* elements = static_cast<const T*>(data);
            for (size_t i = 0; i < newCount - 1; ++i) {
                if (elements[i] > elements[i + 1]) return false;
            }
        }

        // Validate reverse sorted constraint
        if (_containerConstraints.isReverseSorted && newCount > 1) {
            const T* elements = static_cast<const T*>(data);
            for (size_t i = 0; i < newCount - 1; ++i) {
                if (elements[i] < elements[i + 1]) return false;
            }
        }

        return true;
    }

private:
    std::array<T, MaxN> _storage;
    size_t _count;
    ContainerConstraints _containerConstraints;
    ValueConstraints _elementConstraints;
};

// Convenience type aliases
template<size_t MaxN>
using StringProperty = ListProperty<uint8_t, MaxN>;

} // namespace MicroProto

#endif // MICROPROTO_LIST_PROPERTY_H
