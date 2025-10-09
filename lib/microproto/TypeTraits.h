#ifndef MICROPROTO_TYPE_TRAITS_H
#define MICROPROTO_TYPE_TRAITS_H

#include <stdint.h>

namespace MicroProto {

// Type IDs from protocol spec
constexpr uint8_t TYPE_BOOL = 0x01;
constexpr uint8_t TYPE_INT8 = 0x02;
constexpr uint8_t TYPE_UINT8 = 0x03;
constexpr uint8_t TYPE_INT32 = 0x04;
constexpr uint8_t TYPE_FLOAT32 = 0x05;

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

} // namespace MicroProto

#endif // MICROPROTO_TYPE_TRAITS_H
