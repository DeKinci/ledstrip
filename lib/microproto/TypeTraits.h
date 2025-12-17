#ifndef MICROPROTO_TYPE_TRAITS_H
#define MICROPROTO_TYPE_TRAITS_H

#include <stdint.h>
#include <array>

namespace MicroProto {

// Type IDs from protocol spec - Basic types
constexpr uint8_t TYPE_BOOL = 0x01;
constexpr uint8_t TYPE_INT8 = 0x02;
constexpr uint8_t TYPE_UINT8 = 0x03;
constexpr uint8_t TYPE_INT32 = 0x04;
constexpr uint8_t TYPE_FLOAT32 = 0x05;
// 0x06-0x1F reserved for future basic types

// Container types
constexpr uint8_t TYPE_ARRAY = 0x20;   // Fixed-size homogeneous
constexpr uint8_t TYPE_LIST = 0x21;    // Variable-size homogeneous
constexpr uint8_t TYPE_OBJECT = 0x22;  // Fixed-size heterogeneous

// Helper to check if type is a container
constexpr bool isContainerType(uint8_t typeId) {
    return typeId >= 0x20 && typeId <= 0x22;
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

// TypeTraits for std::array (ARRAY container type)
template<typename T, size_t N>
struct TypeTraits<std::array<T, N>> {
    static constexpr uint8_t type_id = TYPE_ARRAY;
    static constexpr uint8_t element_type_id = TypeTraits<T>::type_id;
    static constexpr size_t element_count = N;
    static constexpr size_t element_size = TypeTraits<T>::size;
    static constexpr size_t size = N * element_size;
};

} // namespace MicroProto

#endif // MICROPROTO_TYPE_TRAITS_H
