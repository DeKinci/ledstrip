#ifndef MICROPROTO_REFLECT_H
#define MICROPROTO_REFLECT_H

#include <cstddef>
#include <type_traits>
#include <utility>
#include <tuple>
#include <string_view>
#include <array>

namespace MicroProto {
namespace reflect {

// ============================================================================
// Part 1: Field Count Detection
// Uses aggregate initialization with "any" type to detect field count
// ============================================================================

namespace detail {

// Type that converts to anything - used to probe aggregate initialization
template<typename T, size_t>
struct any_type {
    template<typename U, typename = std::enable_if_t<!std::is_same_v<std::decay_t<U>, T>>>
    constexpr operator U&() const noexcept;

    template<typename U, typename = std::enable_if_t<!std::is_same_v<std::decay_t<U>, T>>>
    constexpr operator U&&() const noexcept;
};

// Test if T is constructible with N arguments
template<typename T, size_t N, typename = void, typename... Args>
struct is_constructible_n : std::false_type {};

template<typename T, size_t N, size_t... Is>
struct is_constructible_n<T, N, std::void_t<decltype(T{any_type<T, Is>{}...})>,
                          std::index_sequence<Is...>> : std::true_type {};

template<typename T, size_t N>
constexpr bool is_constructible_n_v =
    is_constructible_n<T, N, void, std::make_index_sequence<N>>::value;

// Find the exact field count by binary search + linear scan
template<typename T, size_t Lo, size_t Hi>
constexpr size_t field_count_impl() {
    if constexpr (Lo == Hi) {
        return Lo;
    } else {
        constexpr size_t Mid = (Lo + Hi + 1) / 2;
        if constexpr (is_constructible_n_v<T, Mid>) {
            return field_count_impl<T, Mid, Hi>();
        } else {
            return field_count_impl<T, Lo, Mid - 1>();
        }
    }
}

} // namespace detail

/**
 * Get the number of fields in an aggregate type
 */
template<typename T>
constexpr size_t field_count_v = detail::field_count_impl<std::remove_cv_t<T>, 0, sizeof(T)>();

/**
 * Concept for types that can be reflected (aggregates with detectable field count)
 */
template<typename T>
constexpr bool is_reflectable_v =
    std::is_aggregate_v<std::remove_cv_t<T>> &&
    !std::is_array_v<std::remove_cv_t<T>> &&
    field_count_v<T> > 0;

// ============================================================================
// Part 2: Field Name Extraction
// Uses __PRETTY_FUNCTION__ to extract member names at compile time
// ============================================================================

namespace detail {

// Get compiler-specific function signature
#if defined(__clang__) || defined(__GNUC__)
    #define MICROPROTO_PRETTY_FUNCTION __PRETTY_FUNCTION__
#elif defined(_MSC_VER)
    #define MICROPROTO_PRETTY_FUNCTION __FUNCSIG__
#else
    #define MICROPROTO_PRETTY_FUNCTION ""
#endif

// Wrapper to get member pointer into __PRETTY_FUNCTION__
template<auto Ptr>
constexpr std::string_view get_member_name_raw() {
    return MICROPROTO_PRETTY_FUNCTION;
}

// Parse member name from __PRETTY_FUNCTION__ output
// GCC/Clang: "... [Ptr = &StructName::fieldName]"
// We find the last "::" and extract until ']'
constexpr std::string_view parse_member_name(std::string_view sig) {
    // Find the last "::" before the closing bracket
    auto end = sig.rfind(']');
    if (end == std::string_view::npos) end = sig.size();

    // Find "::" before end
    auto pos = sig.rfind("::", end);
    if (pos == std::string_view::npos) {
        // Try finding just the name after '='
        pos = sig.rfind('=', end);
        if (pos != std::string_view::npos) {
            pos += 1;
            while (pos < end && sig[pos] == ' ') ++pos;
        } else {
            return "";
        }
    } else {
        pos += 2; // skip "::"
    }

    // Find end of identifier
    auto name_end = pos;
    while (name_end < end && (sig[name_end] == '_' ||
           (sig[name_end] >= 'a' && sig[name_end] <= 'z') ||
           (sig[name_end] >= 'A' && sig[name_end] <= 'Z') ||
           (sig[name_end] >= '0' && sig[name_end] <= '9'))) {
        ++name_end;
    }

    return sig.substr(pos, name_end - pos);
}

template<auto Ptr>
constexpr std::string_view member_name_v = parse_member_name(get_member_name_raw<Ptr>());

} // namespace detail

// ============================================================================
// Part 3: Structured Binding Decomposition
// Macro-generated overloads for to_tuple (supports up to 16 fields)
// ============================================================================

namespace detail {

// to_tuple overloads - one for each field count
// Returns a tuple of references to the struct's fields

template<typename T>
constexpr auto to_tuple_impl(T& t, std::integral_constant<size_t, 0>) {
    return std::tuple<>();
}

template<typename T>
constexpr auto to_tuple_impl(T& t, std::integral_constant<size_t, 1>) {
    auto& [a] = t;
    return std::tie(a);
}

template<typename T>
constexpr auto to_tuple_impl(T& t, std::integral_constant<size_t, 2>) {
    auto& [a, b] = t;
    return std::tie(a, b);
}

template<typename T>
constexpr auto to_tuple_impl(T& t, std::integral_constant<size_t, 3>) {
    auto& [a, b, c] = t;
    return std::tie(a, b, c);
}

template<typename T>
constexpr auto to_tuple_impl(T& t, std::integral_constant<size_t, 4>) {
    auto& [a, b, c, d] = t;
    return std::tie(a, b, c, d);
}

template<typename T>
constexpr auto to_tuple_impl(T& t, std::integral_constant<size_t, 5>) {
    auto& [a, b, c, d, e] = t;
    return std::tie(a, b, c, d, e);
}

template<typename T>
constexpr auto to_tuple_impl(T& t, std::integral_constant<size_t, 6>) {
    auto& [a, b, c, d, e, f] = t;
    return std::tie(a, b, c, d, e, f);
}

template<typename T>
constexpr auto to_tuple_impl(T& t, std::integral_constant<size_t, 7>) {
    auto& [a, b, c, d, e, f, g] = t;
    return std::tie(a, b, c, d, e, f, g);
}

template<typename T>
constexpr auto to_tuple_impl(T& t, std::integral_constant<size_t, 8>) {
    auto& [a, b, c, d, e, f, g, h] = t;
    return std::tie(a, b, c, d, e, f, g, h);
}

template<typename T>
constexpr auto to_tuple_impl(T& t, std::integral_constant<size_t, 9>) {
    auto& [a, b, c, d, e, f, g, h, i] = t;
    return std::tie(a, b, c, d, e, f, g, h, i);
}

template<typename T>
constexpr auto to_tuple_impl(T& t, std::integral_constant<size_t, 10>) {
    auto& [a, b, c, d, e, f, g, h, i, j] = t;
    return std::tie(a, b, c, d, e, f, g, h, i, j);
}

template<typename T>
constexpr auto to_tuple_impl(T& t, std::integral_constant<size_t, 11>) {
    auto& [a, b, c, d, e, f, g, h, i, j, k] = t;
    return std::tie(a, b, c, d, e, f, g, h, i, j, k);
}

template<typename T>
constexpr auto to_tuple_impl(T& t, std::integral_constant<size_t, 12>) {
    auto& [a, b, c, d, e, f, g, h, i, j, k, l] = t;
    return std::tie(a, b, c, d, e, f, g, h, i, j, k, l);
}

template<typename T>
constexpr auto to_tuple_impl(T& t, std::integral_constant<size_t, 13>) {
    auto& [a, b, c, d, e, f, g, h, i, j, k, l, m] = t;
    return std::tie(a, b, c, d, e, f, g, h, i, j, k, l, m);
}

template<typename T>
constexpr auto to_tuple_impl(T& t, std::integral_constant<size_t, 14>) {
    auto& [a, b, c, d, e, f, g, h, i, j, k, l, m, n] = t;
    return std::tie(a, b, c, d, e, f, g, h, i, j, k, l, m, n);
}

template<typename T>
constexpr auto to_tuple_impl(T& t, std::integral_constant<size_t, 15>) {
    auto& [a, b, c, d, e, f, g, h, i, j, k, l, m, n, o] = t;
    return std::tie(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o);
}

template<typename T>
constexpr auto to_tuple_impl(T& t, std::integral_constant<size_t, 16>) {
    auto& [a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p] = t;
    return std::tie(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p);
}

} // namespace detail

/**
 * Convert aggregate struct to tuple of references
 */
template<typename T>
constexpr auto to_tuple(T& t) {
    return detail::to_tuple_impl(t, std::integral_constant<size_t, field_count_v<std::remove_cv_t<T>>>{});
}

/**
 * Get type of Nth field
 */
template<typename T, size_t N>
using field_type_t = std::remove_reference_t<
    std::tuple_element_t<N, decltype(to_tuple(std::declval<T&>()))>>;

/**
 * Get reference to Nth field
 */
template<size_t N, typename T>
constexpr auto& get(T& t) {
    return std::get<N>(to_tuple(t));
}

template<size_t N, typename T>
constexpr const auto& get(const T& t) {
    return std::get<N>(to_tuple(const_cast<T&>(t)));
}

// ============================================================================
// Part 4: Field Name Access via Member Pointers
// ============================================================================

/**
 * Get pointer to Nth field of type T
 * This is tricky because we need to create a temporary to get the address
 */
namespace detail {

template<typename T, size_t N>
struct field_ptr_helper {
    static constexpr auto get() {
        // Create a fake instance at address 0 and get field offset
        // This only works for getting the member pointer type, not actual pointer
        return nullptr; // Placeholder
    }
};

// Helper to iterate over fields with callback
template<typename T, typename F, size_t... Is>
constexpr void for_each_field_impl(T& t, F&& f, std::index_sequence<Is...>) {
    (f(std::integral_constant<size_t, Is>{}, reflect::get<Is>(t)), ...);
}

} // namespace detail

/**
 * Iterate over all fields of a struct with a callback
 * Callback signature: void(std::integral_constant<size_t, I>, FieldType&)
 */
template<typename T, typename F>
constexpr void for_each_field(T& t, F&& f) {
    detail::for_each_field_impl(t, std::forward<F>(f),
        std::make_index_sequence<field_count_v<std::remove_cv_t<T>>>{});
}

/**
 * Iterate over all fields with index
 * Callback signature: void(size_t index, FieldType&)
 */
template<typename T, typename F>
constexpr void for_each_field_indexed(T& t, F&& f) {
    for_each_field(t, [&f](auto I, auto& field) {
        f(I.value, field);
    });
}

// ============================================================================
// Part 5: Field Info (type, offset, size)
// ============================================================================

/**
 * Information about a single field
 */
struct FieldInfo {
    const char* name;       // Field name (may be nullptr if not extractable)
    uint8_t typeId;         // MicroProto type ID
    uint16_t offset;        // Offset within struct
    uint16_t size;          // Size in bytes
};

/**
 * Build FieldInfo array for a reflectable struct
 * Note: Field names require member pointer access which needs macro support
 */
template<typename T>
constexpr size_t get_field_offset(size_t index) {
    // We can compute this at runtime using actual struct layout
    // This is a placeholder - actual implementation needs per-struct specialization
    return 0;
}

// ============================================================================
// Part 6: Field Names Registration
// Provides compile-time field name access via macro registration
// ============================================================================

/**
 * Primary template - no field names registered (returns nullptr)
 */
template<typename T>
struct field_names {
    static constexpr bool registered = false;
    static constexpr size_t count = 0;

    static constexpr const char* get(size_t) { return nullptr; }
};

/**
 * Helper to check if field names are registered for a type
 */
template<typename T>
inline constexpr bool has_field_names_v = field_names<T>::registered;

/**
 * Get field name by index (returns nullptr if not registered or out of bounds)
 */
template<typename T>
constexpr const char* get_field_name(size_t index) {
    return field_names<T>::get(index);
}

} // namespace reflect
} // namespace MicroProto

/**
 * Macro to register field names for a struct
 *
 * Usage:
 *   struct Position {
 *       int32_t x;
 *       int32_t y;
 *       int32_t z;
 *   };
 *   MICROPROTO_FIELD_NAMES(Position, "x", "y", "z")
 *
 * This creates a specialization of field_names<Position> that returns
 * the field names by index.
 */
#define MICROPROTO_FIELD_NAMES(Type, ...) \
    template<> \
    struct MicroProto::reflect::field_names<Type> { \
        static constexpr bool registered = true; \
        static constexpr const char* names[] = { __VA_ARGS__ }; \
        static constexpr size_t count = sizeof(names) / sizeof(names[0]); \
        static constexpr const char* get(size_t i) { \
            return i < count ? names[i] : nullptr; \
        } \
    }; \
    constexpr const char* MicroProto::reflect::field_names<Type>::names[]

#endif // MICROPROTO_REFLECT_H