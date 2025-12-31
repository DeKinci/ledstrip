#ifndef MICROPROTO_MICRO_LIST_H
#define MICROPROTO_MICRO_LIST_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <initializer_list>
#include <vector>
#include <algorithm>
#include <new>
#include <memory>

namespace MicroProto {

/**
 * MicroList - Small Buffer Optimized dynamic array
 *
 * Stores up to InlineCapacity elements inline (no heap allocation).
 * When capacity is exceeded, allocates on heap with 1.5x growth factor.
 *
 * Supports both trivially copyable types (uses fast memcpy) and
 * non-trivially copyable types like std::string (uses proper constructors).
 *
 * Template parameters:
 *   T              - Element type
 *   InlineCapacity - Number of elements stored inline (default 8)
 *   MaxCapacity    - Maximum allowed capacity (default 256, 0 = unlimited)
 *
 * Usage:
 *   MicroList<uint8_t, 16> pixels;  // 16 bytes inline, fast memcpy
 *   MicroList<std::string, 4> names;  // 4 strings inline, proper copy/move
 */
template<typename T, size_t InlineCapacity = 8, size_t MaxCapacity = 256>
class MicroList {
    // =========== Compile-time validation ===========
    static_assert(!std::is_pointer_v<T>,
        "MicroList cannot hold pointer types. Use value types instead.");
    static_assert(!std::is_reference_v<T>,
        "MicroList cannot hold reference types. Use value types instead.");
    static_assert(!std::is_void_v<T>,
        "MicroList cannot hold void type.");
    static_assert(!std::is_function_v<T>,
        "MicroList cannot hold function types. Use MicroFunction or function pointers.");
    static_assert(!std::is_array_v<T>,
        "MicroList cannot hold C-style arrays. Use std::array<T, N> instead.");
    static_assert(InlineCapacity > 0, "InlineCapacity must be at least 1");
    static_assert(MaxCapacity == 0 || MaxCapacity >= InlineCapacity,
        "MaxCapacity must be >= InlineCapacity (or 0 for unlimited)");

    // Use fast path for trivially copyable types
    static constexpr bool is_trivial = std::is_trivially_copyable_v<T>;

public:
    using value_type = T;
    using size_type = size_t;
    using difference_type = ptrdiff_t;
    using reference = T&;
    using const_reference = const T&;
    using pointer = T*;
    using const_pointer = const T*;
    using iterator = T*;
    using const_iterator = const T*;

    static constexpr size_t inline_capacity = InlineCapacity;
    static constexpr size_t max_capacity = MaxCapacity;

    // =========== Constructors / Destructor ===========

    MicroList() noexcept : _size(0), _capacity(InlineCapacity), _heap(nullptr) {}

    explicit MicroList(size_t count) : _size(0), _capacity(InlineCapacity), _heap(nullptr) {
        resize(count);
    }

    MicroList(size_t count, const T& value) : _size(0), _capacity(InlineCapacity), _heap(nullptr) {
        resize(count, value);
    }

    MicroList(std::initializer_list<T> init) : _size(0), _capacity(InlineCapacity), _heap(nullptr) {
        reserve(init.size());
        for (const auto& v : init) {
            push_back(v);
        }
    }

    template<typename InputIt,
             typename = std::enable_if_t<!std::is_integral_v<InputIt>>>
    MicroList(InputIt first, InputIt last) : _size(0), _capacity(InlineCapacity), _heap(nullptr) {
        for (; first != last; ++first) {
            push_back(*first);
        }
    }

    // Interop: construct from std::vector
    explicit MicroList(const std::vector<T>& vec) : _size(0), _capacity(InlineCapacity), _heap(nullptr) {
        reserve(vec.size());
        for (const auto& v : vec) {
            push_back(v);
        }
    }

    // Copy constructor
    MicroList(const MicroList& other) : _size(0), _capacity(InlineCapacity), _heap(nullptr) {
        reserve(other._size);
        if constexpr (is_trivial) {
            std::memcpy(data(), other.data(), other._size * sizeof(T));
            _size = other._size;
        } else {
            for (size_t i = 0; i < other._size; ++i) {
                new (data() + i) T(other.data()[i]);
            }
            _size = other._size;
        }
    }

    // Move constructor
    MicroList(MicroList&& other) noexcept : _size(0), _capacity(InlineCapacity), _heap(nullptr) {
        if (other.is_inline()) {
            if constexpr (is_trivial) {
                std::memcpy(inline_data(), other.inline_data(), other._size * sizeof(T));
            } else {
                for (size_t i = 0; i < other._size; ++i) {
                    new (inline_data() + i) T(std::move(other.inline_data()[i]));
                    other.inline_data()[i].~T();
                }
            }
            _size = other._size;
            other._size = 0;
        } else {
            _heap = other._heap;
            _size = other._size;
            _capacity = other._capacity;
            other._heap = nullptr;
            other._size = 0;
            other._capacity = InlineCapacity;
        }
    }

    ~MicroList() {
        destroy_all();
        if (_heap) {
            ::operator delete(_heap);
        }
    }

    // =========== Assignment ===========

    MicroList& operator=(const MicroList& other) {
        if (this != &other) {
            clear();
            reserve(other._size);
            if constexpr (is_trivial) {
                std::memcpy(data(), other.data(), other._size * sizeof(T));
                _size = other._size;
            } else {
                for (size_t i = 0; i < other._size; ++i) {
                    new (data() + i) T(other.data()[i]);
                }
                _size = other._size;
            }
        }
        return *this;
    }

    MicroList& operator=(MicroList&& other) noexcept {
        if (this != &other) {
            destroy_all();
            if (_heap) {
                ::operator delete(_heap);
                _heap = nullptr;
            }
            _capacity = InlineCapacity;

            if (other.is_inline()) {
                if constexpr (is_trivial) {
                    std::memcpy(inline_data(), other.inline_data(), other._size * sizeof(T));
                } else {
                    for (size_t i = 0; i < other._size; ++i) {
                        new (inline_data() + i) T(std::move(other.inline_data()[i]));
                        other.inline_data()[i].~T();
                    }
                }
                _size = other._size;
                other._size = 0;
            } else {
                _heap = other._heap;
                _size = other._size;
                _capacity = other._capacity;
                other._heap = nullptr;
                other._size = 0;
                other._capacity = InlineCapacity;
            }
        }
        return *this;
    }

    MicroList& operator=(std::initializer_list<T> init) {
        clear();
        reserve(init.size());
        for (const auto& v : init) {
            push_back(v);
        }
        return *this;
    }

    // Interop: assign from std::vector
    MicroList& operator=(const std::vector<T>& vec) {
        clear();
        reserve(vec.size());
        for (const auto& v : vec) {
            push_back(v);
        }
        return *this;
    }

    // =========== Element Access ===========

    T& operator[](size_t i) { return data()[i]; }
    const T& operator[](size_t i) const { return data()[i]; }

    T& at(size_t i) {
        // Bounds check without exceptions - clamp to valid range
        if (i >= _size) i = _size > 0 ? _size - 1 : 0;
        return data()[i];
    }

    const T& at(size_t i) const {
        if (i >= _size) i = _size > 0 ? _size - 1 : 0;
        return data()[i];
    }

    T& front() { return data()[0]; }
    const T& front() const { return data()[0]; }

    T& back() { return data()[_size - 1]; }
    const T& back() const { return data()[_size - 1]; }

    T* data() noexcept { return is_inline() ? inline_data() : _heap; }
    const T* data() const noexcept { return is_inline() ? inline_data() : _heap; }

    // =========== Iterators ===========

    iterator begin() noexcept { return data(); }
    const_iterator begin() const noexcept { return data(); }
    const_iterator cbegin() const noexcept { return data(); }

    iterator end() noexcept { return data() + _size; }
    const_iterator end() const noexcept { return data() + _size; }
    const_iterator cend() const noexcept { return data() + _size; }

    // =========== Capacity ===========

    bool empty() const noexcept { return _size == 0; }
    size_t size() const noexcept { return _size; }
    size_t capacity() const noexcept { return _capacity; }
    bool is_inline() const noexcept { return _heap == nullptr; }

    constexpr size_t max_size() const noexcept {
        return MaxCapacity > 0 ? MaxCapacity : SIZE_MAX / sizeof(T);
    }

    bool reserve(size_t new_cap) {
        if (new_cap <= _capacity) return true;
        if (MaxCapacity > 0 && new_cap > MaxCapacity) return false;
        return grow_to(new_cap);
    }

    void shrink_to_fit() {
        if (is_inline() || _size == _capacity) return;

        if (_size <= InlineCapacity) {
            // Move back to inline
            T* old_heap = _heap;
            if constexpr (is_trivial) {
                std::memcpy(inline_data(), old_heap, _size * sizeof(T));
            } else {
                for (size_t i = 0; i < _size; ++i) {
                    new (inline_data() + i) T(std::move(old_heap[i]));
                    old_heap[i].~T();
                }
            }
            ::operator delete(old_heap);
            _heap = nullptr;
            _capacity = InlineCapacity;
        } else {
            // Shrink heap allocation
            T* new_heap = static_cast<T*>(::operator new(_size * sizeof(T)));
            if constexpr (is_trivial) {
                std::memcpy(new_heap, _heap, _size * sizeof(T));
            } else {
                for (size_t i = 0; i < _size; ++i) {
                    new (new_heap + i) T(std::move(_heap[i]));
                    _heap[i].~T();
                }
            }
            ::operator delete(_heap);
            _heap = new_heap;
            _capacity = _size;
        }
    }

    // =========== Modifiers ===========

    void clear() noexcept {
        destroy_all();
        _size = 0;
    }

    bool push_back(const T& value) {
        if (_size >= _capacity) {
            if (!grow()) return false;
        }
        new (data() + _size) T(value);
        ++_size;
        return true;
    }

    bool push_back(T&& value) {
        if (_size >= _capacity) {
            if (!grow()) return false;
        }
        new (data() + _size) T(std::move(value));
        ++_size;
        return true;
    }

    template<typename... Args>
    T* emplace_back(Args&&... args) {
        if (_size >= _capacity) {
            if (!grow()) return nullptr;
        }
        T* ptr = data() + _size;
        new (ptr) T(std::forward<Args>(args)...);
        ++_size;
        return ptr;
    }

    void pop_back() {
        if (_size > 0) {
            --_size;
            if constexpr (!is_trivial) {
                data()[_size].~T();
            }
        }
    }

    bool resize(size_t new_size) {
        if (new_size > _capacity) {
            if (!grow_to(new_size)) return false;
        }
        if (new_size > _size) {
            // Construct new elements
            if constexpr (is_trivial) {
                std::memset(data() + _size, 0, (new_size - _size) * sizeof(T));
            } else {
                for (size_t i = _size; i < new_size; ++i) {
                    new (data() + i) T();
                }
            }
        } else if (new_size < _size) {
            // Destroy excess elements
            if constexpr (!is_trivial) {
                for (size_t i = new_size; i < _size; ++i) {
                    data()[i].~T();
                }
            }
        }
        _size = new_size;
        return true;
    }

    bool resize(size_t new_size, const T& value) {
        if (new_size > _capacity) {
            if (!grow_to(new_size)) return false;
        }
        if (new_size > _size) {
            // Construct new elements with value
            for (size_t i = _size; i < new_size; ++i) {
                new (data() + i) T(value);
            }
        } else if (new_size < _size) {
            // Destroy excess elements
            if constexpr (!is_trivial) {
                for (size_t i = new_size; i < _size; ++i) {
                    data()[i].~T();
                }
            }
        }
        _size = new_size;
        return true;
    }

    iterator erase(const_iterator pos) {
        if (pos < begin() || pos >= end()) return end();
        size_t idx = pos - begin();

        if constexpr (is_trivial) {
            std::memmove(data() + idx, data() + idx + 1, (_size - idx - 1) * sizeof(T));
        } else {
            // Move elements down
            for (size_t i = idx; i < _size - 1; ++i) {
                data()[i] = std::move(data()[i + 1]);
            }
            data()[_size - 1].~T();
        }
        --_size;
        return data() + idx;
    }

    iterator erase(const_iterator first, const_iterator last) {
        if (first >= last || first < begin() || last > end()) return end();
        size_t start_idx = first - begin();
        size_t count = last - first;

        if constexpr (is_trivial) {
            std::memmove(data() + start_idx, data() + start_idx + count,
                         (_size - start_idx - count) * sizeof(T));
        } else {
            // Move elements down
            size_t move_count = _size - start_idx - count;
            for (size_t i = 0; i < move_count; ++i) {
                data()[start_idx + i] = std::move(data()[start_idx + count + i]);
            }
            // Destroy trailing elements
            for (size_t i = _size - count; i < _size; ++i) {
                data()[i].~T();
            }
        }
        _size -= count;
        return data() + start_idx;
    }

    iterator insert(const_iterator pos, const T& value) {
        size_t idx = pos - begin();
        if (_size >= _capacity) {
            if (!grow()) return end();
        }

        if constexpr (is_trivial) {
            std::memmove(data() + idx + 1, data() + idx, (_size - idx) * sizeof(T));
            data()[idx] = value;
        } else {
            // Construct new element at end
            if (_size > 0) {
                new (data() + _size) T(std::move(data()[_size - 1]));
                // Move elements up
                for (size_t i = _size - 1; i > idx; --i) {
                    data()[i] = std::move(data()[i - 1]);
                }
                data()[idx] = value;
            } else {
                new (data() + idx) T(value);
            }
        }
        ++_size;
        return data() + idx;
    }

    // =========== Interoperability ===========

    std::vector<T> to_vector() const {
        return std::vector<T>(begin(), end());
    }

    // Assign from raw data (for wire decoding) - only for trivially copyable types
    template<typename U = T>
    std::enable_if_t<std::is_trivially_copyable_v<U>, bool>
    assign(const T* src, size_t count) {
        if (MaxCapacity > 0 && count > MaxCapacity) return false;
        clear();
        if (!reserve(count)) return false;
        std::memcpy(data(), src, count * sizeof(T));
        _size = count;
        return true;
    }

    // =========== Comparison ===========

    bool operator==(const MicroList& other) const {
        if (_size != other._size) return false;
        if constexpr (is_trivial) {
            return std::memcmp(data(), other.data(), _size * sizeof(T)) == 0;
        } else {
            return std::equal(begin(), end(), other.begin());
        }
    }

    bool operator!=(const MicroList& other) const {
        return !(*this == other);
    }

    bool operator==(const std::vector<T>& other) const {
        if (_size != other.size()) return false;
        if constexpr (is_trivial) {
            return std::memcmp(data(), other.data(), _size * sizeof(T)) == 0;
        } else {
            return std::equal(begin(), end(), other.begin());
        }
    }

    bool operator!=(const std::vector<T>& other) const {
        return !(*this == other);
    }

private:
    // Aligned storage for inline elements
    alignas(T) unsigned char _inline_storage[InlineCapacity * sizeof(T)];
    T* _heap;
    size_t _size;
    size_t _capacity;

    T* inline_data() noexcept {
        return reinterpret_cast<T*>(_inline_storage);
    }

    const T* inline_data() const noexcept {
        return reinterpret_cast<const T*>(_inline_storage);
    }

    void destroy_all() {
        if constexpr (!is_trivial) {
            for (size_t i = 0; i < _size; ++i) {
                data()[i].~T();
            }
        }
    }

    bool grow() {
        // 1.5x growth factor
        size_t new_cap = _capacity + (_capacity / 2);
        if (new_cap < _capacity + 1) new_cap = _capacity + 1;
        if (MaxCapacity > 0 && new_cap > MaxCapacity) {
            new_cap = MaxCapacity;
            if (new_cap <= _capacity) return false; // Already at max
        }
        return grow_to(new_cap);
    }

    bool grow_to(size_t new_cap) {
        if (new_cap <= _capacity) return true;
        if (MaxCapacity > 0 && new_cap > MaxCapacity) return false;

        // Allocate new storage
        T* new_data = static_cast<T*>(::operator new(new_cap * sizeof(T), std::nothrow));
        if (!new_data) return false;

        // Move existing elements
        if constexpr (is_trivial) {
            std::memcpy(new_data, data(), _size * sizeof(T));
        } else {
            for (size_t i = 0; i < _size; ++i) {
                new (new_data + i) T(std::move(data()[i]));
                data()[i].~T();
            }
        }

        // Free old heap storage (not inline)
        if (_heap) {
            ::operator delete(_heap);
        }

        _heap = new_data;
        _capacity = new_cap;
        return true;
    }
};

// ============================================================================
// Type traits
// ============================================================================

template<typename T>
struct is_micro_list : std::false_type {};

template<typename T, size_t Inline, size_t Max>
struct is_micro_list<MicroList<T, Inline, Max>> : std::true_type {};

template<typename T>
inline constexpr bool is_micro_list_v = is_micro_list<T>::value;

// Check if a MicroList uses fast (trivial) operations
template<typename T>
struct is_micro_list_trivial : std::false_type {};

template<typename T, size_t Inline, size_t Max>
struct is_micro_list_trivial<MicroList<T, Inline, Max>>
    : std::bool_constant<std::is_trivially_copyable_v<T>> {};

template<typename T>
inline constexpr bool is_micro_list_trivial_v = is_micro_list_trivial<T>::value;

// Get element type
template<typename T>
struct micro_list_element {};

template<typename T, size_t Inline, size_t Max>
struct micro_list_element<MicroList<T, Inline, Max>> {
    using type = T;
};

template<typename T>
using micro_list_element_t = typename micro_list_element<T>::type;

// Get inline capacity
template<typename T>
struct micro_list_inline_capacity {};

template<typename T, size_t Inline, size_t Max>
struct micro_list_inline_capacity<MicroList<T, Inline, Max>> {
    static constexpr size_t value = Inline;
};

template<typename T>
inline constexpr size_t micro_list_inline_capacity_v = micro_list_inline_capacity<T>::value;

// Get max capacity
template<typename T>
struct micro_list_max_capacity {};

template<typename T, size_t Inline, size_t Max>
struct micro_list_max_capacity<MicroList<T, Inline, Max>> {
    static constexpr size_t value = Max;
};

template<typename T>
inline constexpr size_t micro_list_max_capacity_v = micro_list_max_capacity<T>::value;

} // namespace MicroProto

#endif // MICROPROTO_MICRO_LIST_H