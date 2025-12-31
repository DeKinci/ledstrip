#ifndef MICROPROTO_TYPE_TRAITS_H
#define MICROPROTO_TYPE_TRAITS_H

#include <stdint.h>
#include <array>
#include <string>
#include <type_traits>
#include "MicroList.h"

// Forward declaration of Value<T>
namespace MicroProto {
template<typename T> struct Value;
}

namespace MicroProto {

// ============================================================================
// Compile-time type validation
// ============================================================================
// These traits determine which types are valid for MicroProto containers
// and properties. Invalid types will fail with clear compile-time errors.

// Forward declarations
template<typename T> struct is_microproto_basic_type;
template<typename T> struct is_microproto_container;
template<typename T> struct is_microproto_struct;
template<typename T> struct is_microproto_type;

// ---------------------------------------------------------------------------
// Basic types: bool, int8_t, uint8_t, int32_t, float
// ---------------------------------------------------------------------------
template<typename T>
struct is_microproto_basic_type : std::false_type {};

template<> struct is_microproto_basic_type<bool> : std::true_type {};
template<> struct is_microproto_basic_type<int8_t> : std::true_type {};
template<> struct is_microproto_basic_type<uint8_t> : std::true_type {};
template<> struct is_microproto_basic_type<int32_t> : std::true_type {};
template<> struct is_microproto_basic_type<float> : std::true_type {};

// Value<T> is a basic type if T is a basic type (transparent wrapper)
template<typename T>
struct is_microproto_basic_type<Value<T>> : is_microproto_basic_type<T> {};

template<typename T>
inline constexpr bool is_microproto_basic_type_v = is_microproto_basic_type<T>::value;

// ---------------------------------------------------------------------------
// String type: std::string
// Wire format: varint(length) + UTF-8 bytes
// ---------------------------------------------------------------------------
template<typename T>
struct is_microproto_string : std::false_type {};

template<>
struct is_microproto_string<std::string> : std::true_type {};

template<typename T>
inline constexpr bool is_microproto_string_v = is_microproto_string<T>::value;

// ---------------------------------------------------------------------------
// Container types: std::array<T, N> and MicroList<T, I, M>
// Recursively validates that element type is also valid
// ---------------------------------------------------------------------------
template<typename T>
struct is_microproto_container : std::false_type {};

template<typename T, size_t N>
struct is_microproto_container<std::array<T, N>> : is_microproto_type<T> {};

template<typename T, size_t I, size_t M>
struct is_microproto_container<MicroList<T, I, M>> : is_microproto_type<T> {};

template<typename T>
inline constexpr bool is_microproto_container_v = is_microproto_container<T>::value;

// ---------------------------------------------------------------------------
// Struct types: trivially copyable, non-basic, non-container, non-pointer
// ---------------------------------------------------------------------------
template<typename T>
struct is_microproto_struct : std::bool_constant<
    std::is_trivially_copyable_v<T> &&
    !is_microproto_basic_type_v<T> &&
    !is_microproto_container_v<T> &&
    !std::is_pointer_v<T> &&
    !std::is_reference_v<T> &&
    !std::is_array_v<T>  // C-style arrays not allowed
> {};

template<typename T>
inline constexpr bool is_microproto_struct_v = is_microproto_struct<T>::value;

// ---------------------------------------------------------------------------
// Valid MicroProto type: basic OR string OR container OR struct
// ---------------------------------------------------------------------------
template<typename T>
struct is_microproto_type : std::bool_constant<
    is_microproto_basic_type_v<T> ||
    is_microproto_string_v<T> ||
    is_microproto_container_v<T> ||
    is_microproto_struct_v<T>
> {};

template<typename T>
inline constexpr bool is_microproto_type_v = is_microproto_type<T>::value;

// ---------------------------------------------------------------------------
// Fixed-size type: has compile-time known wire size
// Basic types, structs, and arrays of fixed-size types are fixed-size.
// MicroList and containers of MicroList are variable-size.
// ---------------------------------------------------------------------------

// Primary template: fixed if basic type or struct
template<typename T>
struct is_microproto_fixed_size : std::bool_constant<
    is_microproto_basic_type_v<T> || is_microproto_struct_v<T>
> {};

// std::array is fixed-size only if element is fixed-size
template<typename T, size_t N>
struct is_microproto_fixed_size<std::array<T, N>> : is_microproto_fixed_size<T> {};

// MicroList is NEVER fixed-size (has variable count)
template<typename T, size_t I, size_t M>
struct is_microproto_fixed_size<MicroList<T, I, M>> : std::false_type {};

template<typename T>
inline constexpr bool is_microproto_fixed_size_v = is_microproto_fixed_size<T>::value;

// ---------------------------------------------------------------------------
// Error message helpers for static_assert
// ---------------------------------------------------------------------------
template<typename T>
struct microproto_type_error {
    static constexpr bool is_pointer = std::is_pointer_v<T>;
    static constexpr bool is_reference = std::is_reference_v<T>;
    static constexpr bool is_c_array = std::is_array_v<T>;
    static constexpr bool is_function = std::is_function_v<T>;
    static constexpr bool is_void = std::is_void_v<T>;
    static constexpr bool has_no_typetraits = !is_microproto_type_v<T>;
};

// Type IDs from MicroProto protocol spec
// IMPORTANT: These are spec-defined. Do NOT extend without updating the protocol spec.
//
// Basic types (0x01-0x05)
constexpr uint8_t TYPE_BOOL = 0x01;
constexpr uint8_t TYPE_INT8 = 0x02;
constexpr uint8_t TYPE_UINT8 = 0x03;
constexpr uint8_t TYPE_INT32 = 0x04;
constexpr uint8_t TYPE_FLOAT32 = 0x05;
// NOTE: int16/uint16/uint32 intentionally omitted - use int32 for all integers
//
// Container types (0x20-0x24)
constexpr uint8_t TYPE_ARRAY = 0x20;    // Fixed-size homogeneous
constexpr uint8_t TYPE_LIST = 0x21;     // Variable-size homogeneous
constexpr uint8_t TYPE_OBJECT = 0x22;   // Fixed-size heterogeneous (struct)
constexpr uint8_t TYPE_VARIANT = 0x23;  // Tagged union
constexpr uint8_t TYPE_RESOURCE = 0x24; // Header/body split (large data)

// Helper to check if type is a container
constexpr bool isContainerType(uint8_t typeId) {
    return typeId >= 0x20 && typeId <= 0x24;
}

// Helper to check if type is basic
constexpr bool isBasicType(uint8_t typeId) {
    return typeId >= 0x01 && typeId <= 0x05;
}

// Type traits for basic types
template<typename T>
struct TypeTraits;

template<>
struct TypeTraits<bool> {
    static constexpr uint8_t type_id = TYPE_BOOL;
    static constexpr size_t size = 1;
};

template<>
struct TypeTraits<uint8_t> {
    static constexpr uint8_t type_id = TYPE_UINT8;
    static constexpr size_t size = 1;
};

template<>
struct TypeTraits<int8_t> {
    static constexpr uint8_t type_id = TYPE_INT8;
    static constexpr size_t size = 1;
};

template<>
struct TypeTraits<int32_t> {
    static constexpr uint8_t type_id = TYPE_INT32;
    static constexpr size_t size = 4;
};

template<>
struct TypeTraits<float> {
    static constexpr uint8_t type_id = TYPE_FLOAT32;
    static constexpr size_t size = 4;
};

// TypeTraits for Value<T> - transparent wrapper, delegates to T
template<typename T>
struct TypeTraits<Value<T>> {
    static constexpr uint8_t type_id = TypeTraits<T>::type_id;
    static constexpr size_t size = TypeTraits<T>::size;
};

// TypeTraits for std::array (ARRAY container type)
// Supports nested containers: std::array<MicroList<...>, N>, std::array<std::array<...>, N>
template<typename T, size_t N>
struct TypeTraits<std::array<T, N>> {
    static_assert(is_microproto_type_v<T>,
        "std::array element type must be a valid MicroProto type");

    static constexpr uint8_t type_id = TYPE_ARRAY;
    static constexpr uint8_t element_type_id = TypeTraits<T>::type_id;
    static constexpr size_t element_count = N;
    static constexpr bool is_fixed_size = is_microproto_fixed_size_v<T>;

    // element_size and size only available for fixed-size element types
    // For variable-size elements (MicroList), use runtime calculation
    template<typename U = T>
    static constexpr size_t get_element_size() {
        if constexpr (is_microproto_fixed_size_v<U>) {
            return TypeTraits<U>::size;
        } else {
            return 0;  // Variable size - must calculate at runtime
        }
    }

    // Only defined for fixed-size arrays
    static constexpr size_t element_size = get_element_size<T>();
    static constexpr size_t size = is_fixed_size ? N * element_size : 0;
};

// TypeTraits for MicroList (LIST container type)
// Supports nested containers: MicroList<std::array<...>>, MicroList<MicroList<...>>
template<typename T, size_t InlineCapacity, size_t MaxCapacity>
struct TypeTraits<MicroList<T, InlineCapacity, MaxCapacity>> {
    static_assert(is_microproto_type_v<T>,
        "MicroList element type must be a valid MicroProto type");

    static constexpr uint8_t type_id = TYPE_LIST;
    static constexpr uint8_t element_type_id = TypeTraits<T>::type_id;
    static constexpr size_t max_element_count = MaxCapacity;
    static constexpr size_t inline_capacity = InlineCapacity;
    static constexpr bool is_fixed_size = false;  // MicroList is always variable-size

    // element_size only for fixed-size element types
    template<typename U = T>
    static constexpr size_t get_element_size() {
        if constexpr (is_microproto_fixed_size_v<U>) {
            return TypeTraits<U>::size;
        } else {
            return 0;  // Variable size
        }
    }

    static constexpr size_t element_size = get_element_size<T>();
    // No static 'size' - MicroList is variable length
};

} // namespace MicroProto

#endif // MICROPROTO_TYPE_TRAITS_H
