#pragma once

#include <cstddef>
#include <cstring>
#include <type_traits>
#include <utility>

namespace microcore {

/**
 * MicroFunction - A lightweight, fixed-size callable wrapper.
 *
 * Template parameters:
 *   Signature - Function signature, e.g., void(int) or int(float, float)
 *   MaxSize   - Maximum storage for captures (0 = function pointer only)
 *
 * Features:
 *   - No heap allocation ever
 *   - Compile-time size checking
 *   - MaxSize=0 compiles to a raw function pointer (4 bytes)
 *   - Trivial copy/move (just memcpy)
 *
 * Constraints (for MaxSize > 0):
 *   - Callable must be trivially copyable
 *   - Callable must be trivially destructible
 *   - Callable size must fit in MaxSize
 *
 * Usage:
 *   // Function pointer only (4 bytes)
 *   MicroFunction<void(int), 0> fn = [](int x) { printf("%d", x); };
 *
 *   // With captures (4 + MaxSize + 4 bytes)
 *   int* ptr = &value;
 *   MicroFunction<void(int), 8> fn = [ptr](int x) { *ptr = x; };
 */
template<typename Signature, size_t MaxSize = sizeof(void*)>
class MicroFunction;

// === Specialization for MaxSize = 0: pure function pointer ===
template<typename R, typename... Args>
class MicroFunction<R(Args...), 0> {
    R (*_fn)(Args...) = nullptr;

public:
    constexpr MicroFunction() = default;
    constexpr MicroFunction(std::nullptr_t) : _fn(nullptr) {}
    constexpr MicroFunction(R (*fn)(Args...)) : _fn(fn) {}

    // Non-capturing lambdas implicitly convert to function pointer
    template<typename F,
             typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, MicroFunction>>>
    constexpr MicroFunction(F f) : _fn(static_cast<R(*)(Args...)>(f)) {}

    MicroFunction(const MicroFunction&) = default;
    MicroFunction(MicroFunction&&) = default;
    MicroFunction& operator=(const MicroFunction&) = default;
    MicroFunction& operator=(MicroFunction&&) = default;
    MicroFunction& operator=(std::nullptr_t) { _fn = nullptr; return *this; }

    R operator()(Args... args) const {
        return _fn(std::forward<Args>(args)...);
    }

    constexpr explicit operator bool() const { return _fn != nullptr; }
    constexpr void clear() { _fn = nullptr; }

    // For testing/debugging
    constexpr R (*target() const)(Args...) { return _fn; }
};

// === General case: inline storage for captures ===
template<typename R, typename... Args, size_t MaxSize>
class MicroFunction<R(Args...), MaxSize> {
    alignas(alignof(void*)) unsigned char _storage[MaxSize];
    R (*_invoke)(void*, Args...) = nullptr;

public:
    constexpr MicroFunction() : _storage{} {}
    constexpr MicroFunction(std::nullptr_t) : _storage{}, _invoke(nullptr) {}

    template<typename F,
             typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, MicroFunction>>>
    MicroFunction(F f) : _storage{} {
        using Callable = std::decay_t<F>;

        static_assert(sizeof(Callable) <= MaxSize,
            "Callable too large for MicroFunction - increase MaxSize or capture less");
        static_assert(std::is_trivially_copyable_v<Callable>,
            "Callable must be trivially copyable (no std::string, std::function, etc.)");
        static_assert(std::is_trivially_destructible_v<Callable>,
            "Callable must be trivially destructible");

        std::memcpy(_storage, &f, sizeof(Callable));
        _invoke = [](void* ptr, Args... args) -> R {
            return (*static_cast<Callable*>(ptr))(std::forward<Args>(args)...);
        };
    }

    MicroFunction(const MicroFunction&) = default;
    MicroFunction(MicroFunction&&) = default;
    MicroFunction& operator=(const MicroFunction&) = default;
    MicroFunction& operator=(MicroFunction&&) = default;
    MicroFunction& operator=(std::nullptr_t) { _invoke = nullptr; return *this; }

    R operator()(Args... args) const {
        return _invoke(const_cast<void*>(static_cast<const void*>(_storage)),
                       std::forward<Args>(args)...);
    }

    explicit operator bool() const { return _invoke != nullptr; }
    void clear() { _invoke = nullptr; }
};

// Type aliases for common use cases
template<typename Signature>
using FnPtr = MicroFunction<Signature, 0>;

template<typename Signature>
using Fn = MicroFunction<Signature, sizeof(void*)>;

template<typename Signature>
using Fn16 = MicroFunction<Signature, 16>;

} // namespace microcore