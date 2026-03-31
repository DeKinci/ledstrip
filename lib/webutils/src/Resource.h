#pragma once

#include <cstdint>
#include <cstddef>

enum class ResourceEncoding : uint8_t {
    NONE = 0,
    BROTLI = 1,
    GZIP = 2
};

struct Resource {
    const uint8_t* data;
    size_t length;
    const char* contentType;
    ResourceEncoding encoding = ResourceEncoding::NONE;
};
