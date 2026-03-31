#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <FastLED.h>
#include <Reflect.h>

static constexpr size_t MAX_SEGMENTS = 12;
static constexpr size_t SEG_NAME_LEN = 8;

enum SegmentType : uint8_t {
    SEG_LINE   = 0,
    SEG_RING   = 1,
    SEG_MATRIX = 2,
};

// 20 bytes per segment — fits in ListProperty with proper field-level schema
struct SegmentData {
    std::array<uint8_t, SEG_NAME_LEN> name;  // null-terminated (7 chars max)
    uint16_t ledCount;          // number of LEDs
    int16_t x;                  // 2D UI position
    int16_t y;
    int16_t rotation;           // degrees
    uint8_t flags;              // bits 0-1: SegmentType, bit 2: serpentine, bit 3: reverse
    uint8_t width;              // matrix columns (0 for non-matrix)
    uint8_t height;             // matrix rows (0 for non-matrix)
    uint8_t _reserved;
};

MICROPROTO_FIELD_NAMES(SegmentData,
    "name", "ledCount", "x", "y", "rotation", "flags", "width", "height", "_reserved");

// Runtime view — rebuilt on config change, used in hot path
struct SegmentView {
    CRGB* leds;               // pointer into global leds[] at startIndex
    uint16_t ledCount;        // cached
    const SegmentData* data;  // pointer into config
};

// Helpers
inline const char* segName(const SegmentData& seg) {
    return reinterpret_cast<const char*>(seg.name.data());
}

inline SegmentType segType(const SegmentData& seg) {
    return static_cast<SegmentType>(seg.flags & 0x03);
}

inline bool segSerpentine(const SegmentData& seg) {
    return seg.flags & 0x04;
}

inline bool segReversed(const SegmentData& seg) {
    return seg.flags & 0x08;
}
