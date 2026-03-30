#ifdef NATIVE_TEST

#include <unity.h>
#include <Property.h>
#include <ListProperty.h>
#include <ArrayProperty.h>
#include <ObjectProperty.h>
#include <VariantProperty.h>
#include <Reflect.h>
#include <wire/Buffer.h>
#include <wire/TypeCodec.h>
#include <messages/Schema.h>
#include <cstring>

using namespace MicroProto;

void setUp(void) {
    PropertyBase::byId.fill(nullptr);
    PropertyBase::count = 0;
}

void tearDown(void) {}

// ============================================================================
// Test structs — various shapes for nesting tests
// ============================================================================

struct Point {
    int32_t x;
    int32_t y;
};
MICROPROTO_FIELD_NAMES(Point, "x", "y");

struct Color3 {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};
MICROPROTO_FIELD_NAMES(Color3, "r", "g", "b");

struct Particle {
    int32_t x;
    int32_t y;
    uint8_t size;
    uint8_t color;
};
MICROPROTO_FIELD_NAMES(Particle, "x", "y", "size", "color");

// Struct with nested array
struct Pixel {
    std::array<uint8_t, 3> rgb;
    uint8_t alpha;
};
MICROPROTO_FIELD_NAMES(Pixel, "rgb", "alpha");

// Struct with mixed types
struct SensorReading {
    float value;
    uint8_t unit;
    int32_t timestamp;
    bool valid;
};
MICROPROTO_FIELD_NAMES(SensorReading, "value", "unit", "timestamp", "valid");

// Single-field struct
struct Wrapper {
    int32_t inner;
};

// Empty-ish struct (1 field)
struct Flag {
    bool active;
};

// Larger struct (like SegmentData)
struct Segment {
    std::array<uint8_t, 8> name;
    uint16_t ledCount;
    int16_t x;
    int16_t y;
    int16_t rotation;
    uint8_t flags;
    uint8_t width;
    uint8_t height;
    uint8_t reserved;
};
MICROPROTO_FIELD_NAMES(Segment, "name", "ledCount", "x", "y", "rotation",
                       "flags", "width", "height", "reserved");

// ============================================================================
// TypeTraits for struct types
// ============================================================================

void test_type_traits_struct_type_id() {
    TEST_ASSERT_EQUAL(TYPE_OBJECT, TypeTraits<Point>::type_id);
    TEST_ASSERT_EQUAL(TYPE_OBJECT, TypeTraits<Color3>::type_id);
    TEST_ASSERT_EQUAL(TYPE_OBJECT, TypeTraits<Particle>::type_id);
    TEST_ASSERT_EQUAL(TYPE_OBJECT, TypeTraits<SensorReading>::type_id);
    TEST_ASSERT_EQUAL(TYPE_OBJECT, TypeTraits<Segment>::type_id);
}

void test_type_traits_struct_size() {
    TEST_ASSERT_EQUAL(sizeof(Point), TypeTraits<Point>::size);
    TEST_ASSERT_EQUAL(sizeof(Color3), TypeTraits<Color3>::size);
    TEST_ASSERT_EQUAL(sizeof(Particle), TypeTraits<Particle>::size);
    TEST_ASSERT_EQUAL(sizeof(Segment), TypeTraits<Segment>::size);
}

void test_type_traits_basic_unchanged() {
    // Ensure basic types still work after adding struct support
    TEST_ASSERT_EQUAL(TYPE_BOOL, TypeTraits<bool>::type_id);
    TEST_ASSERT_EQUAL(TYPE_UINT8, TypeTraits<uint8_t>::type_id);
    TEST_ASSERT_EQUAL(TYPE_INT8, TypeTraits<int8_t>::type_id);
    TEST_ASSERT_EQUAL(TYPE_INT32, TypeTraits<int32_t>::type_id);
    TEST_ASSERT_EQUAL(TYPE_FLOAT32, TypeTraits<float>::type_id);
    TEST_ASSERT_EQUAL(1, TypeTraits<bool>::size);
    TEST_ASSERT_EQUAL(1, TypeTraits<uint8_t>::size);
    TEST_ASSERT_EQUAL(4, TypeTraits<int32_t>::size);
    TEST_ASSERT_EQUAL(4, TypeTraits<float>::size);
}

void test_type_traits_containers_unchanged() {
    // ARRAY and LIST TypeTraits still work
    TEST_ASSERT_EQUAL(TYPE_ARRAY, (TypeTraits<std::array<uint8_t, 3>>::type_id));
    TEST_ASSERT_EQUAL(3, (TypeTraits<std::array<uint8_t, 3>>::size));
    TEST_ASSERT_EQUAL(TYPE_UINT8, (TypeTraits<std::array<uint8_t, 3>>::element_type_id));
}

// ============================================================================
// encodeBasic / decodeBasic for TYPE_OBJECT
// ============================================================================

void test_encode_basic_object() {
    Point p = {42, -7};
    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));

    TEST_ASSERT_TRUE(TypeCodec::encodeBasic(wb, TYPE_OBJECT, &p, sizeof(Point)));
    TEST_ASSERT_EQUAL(sizeof(Point), wb.position());

    // Verify raw bytes (little-endian int32)
    ReadBuffer rb(buf, wb.position());
    Point decoded;
    TEST_ASSERT_TRUE(TypeCodec::decodeBasic(rb, TYPE_OBJECT, &decoded, sizeof(Point)));
    TEST_ASSERT_EQUAL(42, decoded.x);
    TEST_ASSERT_EQUAL(-7, decoded.y);
}

void test_encode_basic_object_color() {
    Color3 c = {255, 128, 0};
    uint8_t buf[8];
    WriteBuffer wb(buf, sizeof(buf));

    TEST_ASSERT_TRUE(TypeCodec::encodeBasic(wb, TYPE_OBJECT, &c, sizeof(Color3)));
    TEST_ASSERT_EQUAL(sizeof(Color3), wb.position());

    ReadBuffer rb(buf, wb.position());
    Color3 decoded;
    TEST_ASSERT_TRUE(TypeCodec::decodeBasic(rb, TYPE_OBJECT, &decoded, sizeof(Color3)));
    TEST_ASSERT_EQUAL(255, decoded.r);
    TEST_ASSERT_EQUAL(128, decoded.g);
    TEST_ASSERT_EQUAL(0, decoded.b);
}

// ============================================================================
// ArrayProperty<Struct, N> — ARRAY of OBJECT
// ============================================================================

void test_array_of_object_basic() {
    ArrayProperty<Point, 3> points("points", {{10,20}, {30,40}, {50,60}},
                                    PropertyLevel::LOCAL);

    TEST_ASSERT_EQUAL(TYPE_ARRAY, points.getTypeId());
    TEST_ASSERT_EQUAL(TYPE_OBJECT, points.getElementTypeId());
    TEST_ASSERT_EQUAL(sizeof(Point), points.getElementSize());
    TEST_ASSERT_EQUAL(3, points.getElementCount());
    TEST_ASSERT_EQUAL(3 * sizeof(Point), points.getSize());
}

void test_array_of_object_access() {
    ArrayProperty<Point, 3> points("points", {{10,20}, {30,40}, {50,60}},
                                    PropertyLevel::LOCAL);

    TEST_ASSERT_EQUAL(10, points[0].x);
    TEST_ASSERT_EQUAL(20, points[0].y);
    TEST_ASSERT_EQUAL(30, points[1].x);
    TEST_ASSERT_EQUAL(60, points[2].y);
}

void test_array_of_object_encode_decode() {
    ArrayProperty<Point, 3> src("src", {{100, 200}, {-1, -2}, {0, 0}},
                                 PropertyLevel::LOCAL);

    uint8_t buf[128];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(TypeCodec::encodeProperty(wb, &src));
    TEST_ASSERT_EQUAL(3 * sizeof(Point), wb.position());

    ArrayProperty<Point, 3> dst("dst", {{0,0}, {0,0}, {0,0}},
                                 PropertyLevel::LOCAL);
    ReadBuffer rb(buf, wb.position());
    TEST_ASSERT_TRUE(TypeCodec::decodeProperty(rb, &dst));

    TEST_ASSERT_EQUAL(100, dst[0].x);
    TEST_ASSERT_EQUAL(200, dst[0].y);
    TEST_ASSERT_EQUAL(-1, dst[1].x);
    TEST_ASSERT_EQUAL(-2, dst[1].y);
    TEST_ASSERT_EQUAL(0, dst[2].x);
    TEST_ASSERT_EQUAL(0, dst[2].y);
}

void test_array_of_object_small_struct() {
    ArrayProperty<Color3, 4> colors("colors",
        {{255,0,0}, {0,255,0}, {0,0,255}, {128,128,128}},
        PropertyLevel::LOCAL);

    uint8_t buf[64];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(TypeCodec::encodeProperty(wb, &colors));

    ArrayProperty<Color3, 4> decoded("dec", {{0,0,0},{0,0,0},{0,0,0},{0,0,0}},
                                      PropertyLevel::LOCAL);
    ReadBuffer rb(buf, wb.position());
    TEST_ASSERT_TRUE(TypeCodec::decodeProperty(rb, &decoded));

    TEST_ASSERT_EQUAL(255, decoded[0].r);
    TEST_ASSERT_EQUAL(0, decoded[0].g);
    TEST_ASSERT_EQUAL(255, decoded[2].b);
    TEST_ASSERT_EQUAL(128, decoded[3].r);
}

void test_array_of_object_single_element() {
    ArrayProperty<Wrapper, 1> single("single", {{42}}, PropertyLevel::LOCAL);

    uint8_t buf[16];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(TypeCodec::encodeProperty(wb, &single));

    ArrayProperty<Wrapper, 1> decoded("dec", {{0}}, PropertyLevel::LOCAL);
    ReadBuffer rb(buf, wb.position());
    TEST_ASSERT_TRUE(TypeCodec::decodeProperty(rb, &decoded));

    TEST_ASSERT_EQUAL(42, decoded[0].inner);
}

// ============================================================================
// ListProperty<Struct, N> — LIST of OBJECT
// ============================================================================

void test_list_of_object_basic() {
    ListProperty<Point, 8> points("points", PropertyLevel::LOCAL);

    TEST_ASSERT_EQUAL(TYPE_LIST, points.getTypeId());
    TEST_ASSERT_EQUAL(TYPE_OBJECT, points.getElementTypeId());
    TEST_ASSERT_EQUAL(sizeof(Point), points.getElementSize());
    TEST_ASSERT_EQUAL(0, points.count());
}

void test_list_of_object_push() {
    ListProperty<Point, 4> points("points", PropertyLevel::LOCAL);

    Point p1 = {10, 20};
    Point p2 = {30, 40};
    TEST_ASSERT_TRUE(points.push(p1));
    TEST_ASSERT_TRUE(points.push(p2));
    TEST_ASSERT_EQUAL(2, points.count());
    TEST_ASSERT_EQUAL(10, points[0].x);
    TEST_ASSERT_EQUAL(40, points[1].y);
}

void test_list_of_object_push_to_capacity() {
    ListProperty<Point, 2> points("points", PropertyLevel::LOCAL);

    TEST_ASSERT_TRUE(points.push({1, 2}));
    TEST_ASSERT_TRUE(points.push({3, 4}));
    TEST_ASSERT_FALSE(points.push({5, 6}));  // Full
    TEST_ASSERT_EQUAL(2, points.count());
}

void test_list_of_object_pop() {
    ListProperty<Color3, 4> colors("colors", PropertyLevel::LOCAL);

    colors.push({255, 0, 0});
    colors.push({0, 255, 0});
    colors.push({0, 0, 255});
    TEST_ASSERT_EQUAL(3, colors.count());

    colors.pop();
    TEST_ASSERT_EQUAL(2, colors.count());
    TEST_ASSERT_EQUAL(0, colors[1].r);
    TEST_ASSERT_EQUAL(255, colors[1].g);
}

void test_list_of_object_encode_decode() {
    ListProperty<Point, 8> src("src", PropertyLevel::LOCAL);
    src.push({100, 200});
    src.push({-50, -100});
    src.push({0, 0});

    uint8_t buf[256];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(TypeCodec::encodeProperty(wb, &src));

    // Wire format: varint(3) + 3 * sizeof(Point)
    TEST_ASSERT_EQUAL(1 + 3 * sizeof(Point), wb.position());

    // Verify varint count
    TEST_ASSERT_EQUAL_HEX8(0x03, buf[0]);

    ListProperty<Point, 8> dst("dst", PropertyLevel::LOCAL);
    ReadBuffer rb(buf, wb.position());
    TEST_ASSERT_TRUE(TypeCodec::decodeProperty(rb, &dst));

    TEST_ASSERT_EQUAL(3, dst.count());
    TEST_ASSERT_EQUAL(100, dst[0].x);
    TEST_ASSERT_EQUAL(200, dst[0].y);
    TEST_ASSERT_EQUAL(-50, dst[1].x);
    TEST_ASSERT_EQUAL(-100, dst[1].y);
    TEST_ASSERT_EQUAL(0, dst[2].x);
}

void test_list_of_object_encode_empty() {
    ListProperty<Point, 4> empty("empty", PropertyLevel::LOCAL);

    uint8_t buf[16];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(TypeCodec::encodeProperty(wb, &empty));

    // Wire format: varint(0)
    TEST_ASSERT_EQUAL(1, wb.position());
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[0]);

    ListProperty<Point, 4> decoded("dec", PropertyLevel::LOCAL);
    ReadBuffer rb(buf, wb.position());
    TEST_ASSERT_TRUE(TypeCodec::decodeProperty(rb, &decoded));
    TEST_ASSERT_EQUAL(0, decoded.count());
}

void test_list_of_object_decode_truncates_at_capacity() {
    // Encode 5 elements but decode into capacity-3 list
    ListProperty<Point, 8> src("src", PropertyLevel::LOCAL);
    for (int i = 0; i < 5; i++) src.push({i * 10, i * 20});

    uint8_t buf[256];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(TypeCodec::encodeProperty(wb, &src));

    ListProperty<Point, 3> dst("dst", PropertyLevel::LOCAL);
    ReadBuffer rb(buf, wb.position());
    TEST_ASSERT_TRUE(TypeCodec::decodeProperty(rb, &dst));

    // Should decode only 3 elements and skip remaining
    TEST_ASSERT_EQUAL(3, dst.count());
    TEST_ASSERT_EQUAL(0, dst[0].x);
    TEST_ASSERT_EQUAL(10, dst[1].x);
    TEST_ASSERT_EQUAL(20, dst[2].x);
}

void test_list_of_object_setdata() {
    ListProperty<Color3, 4> colors("colors", PropertyLevel::LOCAL);

    Color3 data[] = {{255, 0, 0}, {0, 255, 0}};
    colors.setData(data, sizeof(data));

    TEST_ASSERT_EQUAL(2, colors.count());
    TEST_ASSERT_EQUAL(255, colors[0].r);
    TEST_ASSERT_EQUAL(255, colors[1].g);
}

// ============================================================================
// Struct with nested array (OBJECT containing ARRAY)
// ============================================================================

void test_object_with_array_field_encode_decode() {
    ObjectProperty<Pixel> src("src");
    src->rgb = {255, 128, 64};
    src->alpha = 200;
    src.markChanged();

    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(TypeCodec::encodeProperty(wb, &src));
    TEST_ASSERT_EQUAL(sizeof(Pixel), wb.position());

    ObjectProperty<Pixel> dst("dst");
    ReadBuffer rb(buf, wb.position());
    TEST_ASSERT_TRUE(TypeCodec::decodeProperty(rb, &dst));

    TEST_ASSERT_EQUAL(255, dst->rgb[0]);
    TEST_ASSERT_EQUAL(128, dst->rgb[1]);
    TEST_ASSERT_EQUAL(64, dst->rgb[2]);
    TEST_ASSERT_EQUAL(200, dst->alpha);
}

void test_array_of_object_with_array_field() {
    // ARRAY(OBJECT{ARRAY(UINT8, 3), UINT8}) — nested containers
    ArrayProperty<Pixel, 2> src("src", {{{255,0,0}, 255}, {{0,255,0}, 128}},
                                 PropertyLevel::LOCAL);

    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(TypeCodec::encodeProperty(wb, &src));

    ArrayProperty<Pixel, 2> dst("dst", {{{0,0,0}, 0}, {{0,0,0}, 0}},
                                 PropertyLevel::LOCAL);
    ReadBuffer rb(buf, wb.position());
    TEST_ASSERT_TRUE(TypeCodec::decodeProperty(rb, &dst));

    TEST_ASSERT_EQUAL(255, dst[0].rgb[0]);
    TEST_ASSERT_EQUAL(0, dst[0].rgb[1]);
    TEST_ASSERT_EQUAL(255, dst[0].alpha);
    TEST_ASSERT_EQUAL(0, dst[1].rgb[0]);
    TEST_ASSERT_EQUAL(255, dst[1].rgb[1]);
    TEST_ASSERT_EQUAL(128, dst[1].alpha);
}

void test_list_of_object_with_array_field() {
    ListProperty<Pixel, 4> src("src", PropertyLevel::LOCAL);
    src.push({{10, 20, 30}, 100});
    src.push({{40, 50, 60}, 200});

    uint8_t buf[64];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(TypeCodec::encodeProperty(wb, &src));

    ListProperty<Pixel, 4> dst("dst", PropertyLevel::LOCAL);
    ReadBuffer rb(buf, wb.position());
    TEST_ASSERT_TRUE(TypeCodec::decodeProperty(rb, &dst));

    TEST_ASSERT_EQUAL(2, dst.count());
    TEST_ASSERT_EQUAL(10, dst[0].rgb[0]);
    TEST_ASSERT_EQUAL(30, dst[0].rgb[2]);
    TEST_ASSERT_EQUAL(100, dst[0].alpha);
    TEST_ASSERT_EQUAL(60, dst[1].rgb[2]);
    TEST_ASSERT_EQUAL(200, dst[1].alpha);
}

// ============================================================================
// Mixed-type struct (float + uint8 + int32 + bool)
// ============================================================================

void test_list_of_mixed_type_object() {
    ListProperty<SensorReading, 4> src("src", PropertyLevel::LOCAL);
    src.push({3.14f, 1, 1000, true});
    src.push({-273.15f, 2, 2000, false});
    src.push({0.0f, 0, 0, true});

    uint8_t buf[256];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(TypeCodec::encodeProperty(wb, &src));

    ListProperty<SensorReading, 4> dst("dst", PropertyLevel::LOCAL);
    ReadBuffer rb(buf, wb.position());
    TEST_ASSERT_TRUE(TypeCodec::decodeProperty(rb, &dst));

    TEST_ASSERT_EQUAL(3, dst.count());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.14f, dst[0].value);
    TEST_ASSERT_EQUAL(1, dst[0].unit);
    TEST_ASSERT_EQUAL(1000, dst[0].timestamp);
    TEST_ASSERT_TRUE(dst[0].valid);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, -273.15f, dst[1].value);
    TEST_ASSERT_EQUAL(2, dst[1].unit);
    TEST_ASSERT_FALSE(dst[1].valid);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, dst[2].value);
    TEST_ASSERT_TRUE(dst[2].valid);
}

// ============================================================================
// Segment-like real-world struct (the actual use case)
// ============================================================================

void test_list_of_segments_encode_decode() {
    ListProperty<Segment, 12> src("segments", PropertyLevel::LOCAL);

    Segment s1 = {};
    memcpy(s1.name.data(), "ring", 4);
    s1.ledCount = 24;
    s1.x = -50;
    s1.y = 30;
    s1.rotation = 90;
    s1.flags = 1;  // SEG_RING
    src.push(s1);

    Segment s2 = {};
    memcpy(s2.name.data(), "strip", 5);
    s2.ledCount = 100;
    s2.x = 0;
    s2.y = 0;
    s2.rotation = 0;
    s2.flags = 0;  // SEG_LINE
    src.push(s2);

    Segment s3 = {};
    memcpy(s3.name.data(), "matrix", 6);
    s3.ledCount = 64;
    s3.x = 100;
    s3.y = -80;
    s3.rotation = -45;
    s3.flags = 2 | 0x04;  // SEG_MATRIX | serpentine
    s3.width = 8;
    s3.height = 8;
    src.push(s3);

    uint8_t buf[512];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(TypeCodec::encodeProperty(wb, &src));

    ListProperty<Segment, 12> dst("dst", PropertyLevel::LOCAL);
    ReadBuffer rb(buf, wb.position());
    TEST_ASSERT_TRUE(TypeCodec::decodeProperty(rb, &dst));

    TEST_ASSERT_EQUAL(3, dst.count());

    // ring
    TEST_ASSERT_EQUAL_STRING("ring", (const char*)dst[0].name.data());
    TEST_ASSERT_EQUAL(24, dst[0].ledCount);
    TEST_ASSERT_EQUAL(-50, dst[0].x);
    TEST_ASSERT_EQUAL(30, dst[0].y);
    TEST_ASSERT_EQUAL(90, dst[0].rotation);
    TEST_ASSERT_EQUAL(1, dst[0].flags & 0x03);

    // strip
    TEST_ASSERT_EQUAL_STRING("strip", (const char*)dst[1].name.data());
    TEST_ASSERT_EQUAL(100, dst[1].ledCount);
    TEST_ASSERT_EQUAL(0, dst[1].flags & 0x03);

    // matrix
    TEST_ASSERT_EQUAL_STRING("matrix", (const char*)dst[2].name.data());
    TEST_ASSERT_EQUAL(64, dst[2].ledCount);
    TEST_ASSERT_EQUAL(100, dst[2].x);
    TEST_ASSERT_EQUAL(-80, dst[2].y);
    TEST_ASSERT_EQUAL(-45, dst[2].rotation);
    TEST_ASSERT_EQUAL(2, dst[2].flags & 0x03);
    TEST_ASSERT_TRUE(dst[2].flags & 0x04);  // serpentine
    TEST_ASSERT_EQUAL(8, dst[2].width);
    TEST_ASSERT_EQUAL(8, dst[2].height);
}

void test_list_of_segments_max_capacity() {
    ListProperty<Segment, 12> segs("segs", PropertyLevel::LOCAL);

    for (int i = 0; i < 12; i++) {
        Segment s = {};
        char name[8];
        snprintf(name, sizeof(name), "s%d", i);
        memcpy(s.name.data(), name, strlen(name));
        s.ledCount = i + 1;
        TEST_ASSERT_TRUE(segs.push(s));
    }
    TEST_ASSERT_EQUAL(12, segs.count());
    TEST_ASSERT_FALSE(segs.push(Segment{}));  // Full

    // Roundtrip
    uint8_t buf[512];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(TypeCodec::encodeProperty(wb, &segs));

    ListProperty<Segment, 12> dst("dst", PropertyLevel::LOCAL);
    ReadBuffer rb(buf, wb.position());
    TEST_ASSERT_TRUE(TypeCodec::decodeProperty(rb, &dst));

    TEST_ASSERT_EQUAL(12, dst.count());
    TEST_ASSERT_EQUAL_STRING("s0", (const char*)dst[0].name.data());
    TEST_ASSERT_EQUAL(1, dst[0].ledCount);
    TEST_ASSERT_EQUAL_STRING("s11", (const char*)dst[11].name.data());
    TEST_ASSERT_EQUAL(12, dst[11].ledCount);
}

// ============================================================================
// Schema encoding for containers of objects
// ============================================================================

void test_schema_array_of_object() {
    uint8_t buf[128];
    WriteBuffer wb(buf, sizeof(buf));

    ArrayProperty<Point, 3> points("points", {{0,0},{0,0},{0,0}},
                                    PropertyLevel::LOCAL);
    TEST_ASSERT_TRUE(points.encodeTypeDefinition(wb));

    // Expected: TYPE_ARRAY + varint(3) + TYPE_OBJECT + varint(2) + field defs
    size_t pos = 0;
    TEST_ASSERT_EQUAL_HEX8(TYPE_ARRAY, buf[pos++]);
    TEST_ASSERT_EQUAL_HEX8(0x03, buf[pos++]);  // element count = 3

    // Element type: OBJECT with 2 fields
    TEST_ASSERT_EQUAL_HEX8(TYPE_OBJECT, buf[pos++]);
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[pos++]);  // field count = 2

    // Field "x": ident + INT32
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[pos++]);  // name length = 1
    TEST_ASSERT_EQUAL('x', buf[pos++]);
    TEST_ASSERT_EQUAL_HEX8(TYPE_INT32, buf[pos++]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[pos++]);  // no constraints

    // Field "y": ident + INT32
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[pos++]);
    TEST_ASSERT_EQUAL('y', buf[pos++]);
    TEST_ASSERT_EQUAL_HEX8(TYPE_INT32, buf[pos++]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[pos++]);
}

void test_schema_list_of_object() {
    uint8_t buf[128];
    WriteBuffer wb(buf, sizeof(buf));

    ListProperty<Color3, 8> colors("colors", PropertyLevel::LOCAL);
    TEST_ASSERT_TRUE(colors.encodeTypeDefinition(wb));

    // Parse the schema to find OBJECT type
    size_t pos = 0;
    TEST_ASSERT_EQUAL_HEX8(TYPE_LIST, buf[pos++]);

    // Skip container constraints (flags byte + optional varints)
    uint8_t constraintFlags = buf[pos++];
    if (constraintFlags & 0x01) { while (buf[pos++] & 0x80); }  // skip minLength varint
    if (constraintFlags & 0x02) { while (buf[pos++] & 0x80); }  // skip maxLength varint

    // Element type: OBJECT with 3 fields
    TEST_ASSERT_EQUAL_HEX8(TYPE_OBJECT, buf[pos++]);
    TEST_ASSERT_EQUAL_HEX8(0x03, buf[pos++]);  // 3 fields (r, g, b)
}

void test_schema_list_of_segment() {
    uint8_t buf[256];
    WriteBuffer wb(buf, sizeof(buf));

    ListProperty<Segment, 12> segs("segs", PropertyLevel::LOCAL);
    TEST_ASSERT_TRUE(segs.encodeTypeDefinition(wb));

    // Should encode as LIST(OBJECT{9 fields})
    size_t pos = 0;
    TEST_ASSERT_EQUAL_HEX8(TYPE_LIST, buf[pos++]);

    // Skip container constraints
    uint8_t constraintFlags = buf[pos++];
    if (constraintFlags & 0x01) { while (buf[pos++] & 0x80); }
    if (constraintFlags & 0x02) { while (buf[pos++] & 0x80); }

    // Element: OBJECT
    TEST_ASSERT_EQUAL_HEX8(TYPE_OBJECT, buf[pos++]);
    // Field count: Segment has 9 fields
    TEST_ASSERT_EQUAL_HEX8(0x09, buf[pos++]);

    // First field: "name" -> ARRAY of UINT8
    uint8_t nameLen = buf[pos++];
    TEST_ASSERT_EQUAL(4, nameLen);  // "name"
    TEST_ASSERT_EQUAL('n', buf[pos]);
}

// ============================================================================
// Edge cases
// ============================================================================

void test_encode_decode_object_all_zeros() {
    ListProperty<Point, 4> src("src", PropertyLevel::LOCAL);
    src.push({0, 0});

    uint8_t buf[64];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(TypeCodec::encodeProperty(wb, &src));

    ListProperty<Point, 4> dst("dst", PropertyLevel::LOCAL);
    ReadBuffer rb(buf, wb.position());
    TEST_ASSERT_TRUE(TypeCodec::decodeProperty(rb, &dst));

    TEST_ASSERT_EQUAL(1, dst.count());
    TEST_ASSERT_EQUAL(0, dst[0].x);
    TEST_ASSERT_EQUAL(0, dst[0].y);
}

void test_encode_decode_object_negative_values() {
    ListProperty<Point, 4> src("src", PropertyLevel::LOCAL);
    src.push({-2147483647, 2147483647});  // near int32 limits

    uint8_t buf[64];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(TypeCodec::encodeProperty(wb, &src));

    ListProperty<Point, 4> dst("dst", PropertyLevel::LOCAL);
    ReadBuffer rb(buf, wb.position());
    TEST_ASSERT_TRUE(TypeCodec::decodeProperty(rb, &dst));

    TEST_ASSERT_EQUAL(-2147483647, dst[0].x);
    TEST_ASSERT_EQUAL(2147483647, dst[0].y);
}

void test_decode_truncated_buffer_fails() {
    ListProperty<Point, 4> src("src", PropertyLevel::LOCAL);
    src.push({100, 200});

    uint8_t buf[64];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(TypeCodec::encodeProperty(wb, &src));

    // Give only half the buffer to decode
    ListProperty<Point, 4> dst("dst", PropertyLevel::LOCAL);
    ReadBuffer rb(buf, wb.position() / 2);
    TEST_ASSERT_FALSE(TypeCodec::decodeProperty(rb, &dst));
}

void test_decode_empty_buffer_fails() {
    ListProperty<Point, 4> dst("dst", PropertyLevel::LOCAL);
    ReadBuffer rb(nullptr, 0);
    TEST_ASSERT_FALSE(TypeCodec::decodeProperty(rb, &dst));
}

void test_list_of_bool_struct() {
    // Single-field bool struct
    ListProperty<Flag, 8> src("src", PropertyLevel::LOCAL);
    src.push({true});
    src.push({false});
    src.push({true});

    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(TypeCodec::encodeProperty(wb, &src));

    ListProperty<Flag, 8> dst("dst", PropertyLevel::LOCAL);
    ReadBuffer rb(buf, wb.position());
    TEST_ASSERT_TRUE(TypeCodec::decodeProperty(rb, &dst));

    TEST_ASSERT_EQUAL(3, dst.count());
    TEST_ASSERT_TRUE(dst[0].active);
    TEST_ASSERT_FALSE(dst[1].active);
    TEST_ASSERT_TRUE(dst[2].active);
}

void test_object_property_decode_within_buffer_limit() {
    // ObjectProperty with struct that fits in MICROPROTO_DECODE_BUFFER_SIZE (256)
    // Segment = 20 bytes — well within limit
    ObjectProperty<Segment> src("src");
    memcpy(src->name.data(), "test", 4);
    src->ledCount = 42;
    src->x = -100;
    src->y = 50;
    src.markChanged();

    uint8_t buf[64];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(TypeCodec::encodeProperty(wb, &src));

    ObjectProperty<Segment> dst("dst");
    ReadBuffer rb(buf, wb.position());
    TEST_ASSERT_TRUE(TypeCodec::decodeProperty(rb, &dst));

    TEST_ASSERT_EQUAL_STRING("test", (const char*)dst->name.data());
    TEST_ASSERT_EQUAL(42, dst->ledCount);
    TEST_ASSERT_EQUAL(-100, dst->x);
    TEST_ASSERT_EQUAL(50, dst->y);
}

// ============================================================================
// MicroVariant — nestable variant value type
// ============================================================================

// Register variant type definitions
MICROPROTO_VARIANT_TYPES(MicroProto::MicroVariant<4>, 3,
    MicroProto::VariantTypeDef("value", MicroProto::TYPE_UINT8, 1),
    MicroProto::VariantTypeDef("error", MicroProto::TYPE_INT32, 4),
    MicroProto::VariantTypeDef("ratio", MicroProto::TYPE_FLOAT32, 4)
);

void test_micro_variant_type_traits() {
    TEST_ASSERT_EQUAL(TYPE_VARIANT, (TypeTraits<MicroVariant<4>>::type_id));
    TEST_ASSERT_EQUAL(5, (TypeTraits<MicroVariant<4>>::size));  // 1 + 4
    TEST_ASSERT_EQUAL(TYPE_VARIANT, (TypeTraits<MicroVariant<1>>::type_id));
    TEST_ASSERT_EQUAL(2, (TypeTraits<MicroVariant<1>>::size));  // 1 + 1
}

void test_micro_variant_is_wire_safe() {
    static_assert(detail::is_wire_safe_impl<MicroVariant<4>>::value, "MicroVariant should be wire-safe");
    TEST_PASS();
}

void test_micro_variant_not_struct() {
    // MicroVariant should NOT be classified as struct
    static_assert(!is_microproto_struct_v<MicroVariant<4>>, "MicroVariant should not be a struct");
    static_assert(is_micro_variant_v<MicroVariant<4>>, "Should be detected as variant");
    static_assert(is_microproto_type_v<MicroVariant<4>>, "Should be a valid MicroProto type");
    static_assert(is_microproto_fixed_size_v<MicroVariant<4>>, "Should be fixed size");
    TEST_PASS();
}

void test_micro_variant_set_get() {
    MicroVariant<4> v;
    v.set<uint8_t>(0, 42);
    TEST_ASSERT_EQUAL(0, v.typeIndex);
    TEST_ASSERT_EQUAL(42, v.get<uint8_t>());

    v.set<int32_t>(1, -999);
    TEST_ASSERT_EQUAL(1, v.typeIndex);
    TEST_ASSERT_EQUAL(-999, v.get<int32_t>());

    v.set<float>(2, 3.14f);
    TEST_ASSERT_EQUAL(2, v.typeIndex);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.14f, v.get<float>());
}

void test_micro_variant_encode_decode_basic() {
    MicroVariant<4> src;
    src.set<int32_t>(1, 12345);

    uint8_t buf[16];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(TypeCodec::encodeBasic(wb, TYPE_VARIANT, &src, sizeof(src)));
    TEST_ASSERT_EQUAL(sizeof(src), wb.position());

    MicroVariant<4> dst;
    ReadBuffer rb(buf, wb.position());
    TEST_ASSERT_TRUE(TypeCodec::decodeBasic(rb, TYPE_VARIANT, &dst, sizeof(dst)));
    TEST_ASSERT_EQUAL(1, dst.typeIndex);
    TEST_ASSERT_EQUAL(12345, dst.get<int32_t>());
}

// Struct containing a variant field
struct StatusMessage {
    uint8_t code;
    MicroVariant<4> payload;
};
MICROPROTO_FIELD_NAMES(StatusMessage, "code", "payload");

void test_object_with_variant_field() {
    ObjectProperty<StatusMessage> src("src");
    src->code = 1;
    src->payload.set<int32_t>(1, -42);
    src.markChanged();

    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(TypeCodec::encodeProperty(wb, &src));

    ObjectProperty<StatusMessage> dst("dst");
    ReadBuffer rb(buf, wb.position());
    TEST_ASSERT_TRUE(TypeCodec::decodeProperty(rb, &dst));

    TEST_ASSERT_EQUAL(1, dst->code);
    TEST_ASSERT_EQUAL(1, dst->payload.typeIndex);
    TEST_ASSERT_EQUAL(-42, dst->payload.get<int32_t>());
}

void test_array_of_variant() {
    ArrayProperty<MicroVariant<4>, 3> src("src",
        {MicroVariant<4>{}, MicroVariant<4>{}, MicroVariant<4>{}},
        PropertyLevel::LOCAL);

    // Set different types in each slot
    auto arr = src.get();
    arr[0].set<uint8_t>(0, 255);
    arr[1].set<int32_t>(1, -1000);
    arr[2].set<float>(2, 2.718f);
    src.set(arr);

    uint8_t buf[64];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(TypeCodec::encodeProperty(wb, &src));
    TEST_ASSERT_EQUAL(3 * sizeof(MicroVariant<4>), wb.position());

    ArrayProperty<MicroVariant<4>, 3> dst("dst",
        {MicroVariant<4>{}, MicroVariant<4>{}, MicroVariant<4>{}},
        PropertyLevel::LOCAL);
    ReadBuffer rb(buf, wb.position());
    TEST_ASSERT_TRUE(TypeCodec::decodeProperty(rb, &dst));

    TEST_ASSERT_EQUAL(0, dst[0].typeIndex);
    TEST_ASSERT_EQUAL(255, dst[0].get<uint8_t>());
    TEST_ASSERT_EQUAL(1, dst[1].typeIndex);
    TEST_ASSERT_EQUAL(-1000, dst[1].get<int32_t>());
    TEST_ASSERT_EQUAL(2, dst[2].typeIndex);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.718f, dst[2].get<float>());
}

void test_list_of_variant() {
    ListProperty<MicroVariant<4>, 8> src("src", PropertyLevel::LOCAL);

    MicroVariant<4> v1; v1.set<uint8_t>(0, 100);
    MicroVariant<4> v2; v2.set<int32_t>(1, 999);
    src.push(v1);
    src.push(v2);

    uint8_t buf[64];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(TypeCodec::encodeProperty(wb, &src));

    ListProperty<MicroVariant<4>, 8> dst("dst", PropertyLevel::LOCAL);
    ReadBuffer rb(buf, wb.position());
    TEST_ASSERT_TRUE(TypeCodec::decodeProperty(rb, &dst));

    TEST_ASSERT_EQUAL(2, dst.count());
    TEST_ASSERT_EQUAL(0, dst[0].typeIndex);
    TEST_ASSERT_EQUAL(100, dst[0].get<uint8_t>());
    TEST_ASSERT_EQUAL(1, dst[1].typeIndex);
    TEST_ASSERT_EQUAL(999, dst[1].get<int32_t>());
}

void test_variant_types_registered() {
    TEST_ASSERT_TRUE((reflect::variant_types<MicroVariant<4>>::registered));
    TEST_ASSERT_EQUAL(3, (reflect::variant_types<MicroVariant<4>>::count));
    TEST_ASSERT_EQUAL_STRING("value", reflect::variant_types<MicroVariant<4>>::types[0].name);
    TEST_ASSERT_EQUAL(TYPE_UINT8, reflect::variant_types<MicroVariant<4>>::types[0].typeId);
    TEST_ASSERT_EQUAL_STRING("error", reflect::variant_types<MicroVariant<4>>::types[1].name);
    TEST_ASSERT_EQUAL(TYPE_INT32, reflect::variant_types<MicroVariant<4>>::types[1].typeId);
}

void test_schema_variant_in_object() {
    uint8_t buf[128];
    WriteBuffer wb(buf, sizeof(buf));

    ObjectProperty<StatusMessage> prop("prop");
    TEST_ASSERT_TRUE(prop.encodeTypeDefinition(wb));

    // Should contain OBJECT with 2 fields: code(UINT8) + payload(VARIANT)
    size_t pos = 0;
    TEST_ASSERT_EQUAL_HEX8(TYPE_OBJECT, buf[pos++]);
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[pos++]);  // 2 fields

    // Field "code": ident + UINT8
    TEST_ASSERT_EQUAL_HEX8(0x04, buf[pos++]);  // name length = 4
    TEST_ASSERT_EQUAL('c', buf[pos]);
    pos += 4;  // skip "code"
    TEST_ASSERT_EQUAL_HEX8(TYPE_UINT8, buf[pos++]);
    pos++;  // constraints byte

    // Field "payload": ident + VARIANT
    TEST_ASSERT_EQUAL_HEX8(0x07, buf[pos++]);  // name length = 7
    TEST_ASSERT_EQUAL('p', buf[pos]);
    pos += 7;  // skip "payload"
    TEST_ASSERT_EQUAL_HEX8(TYPE_VARIANT, buf[pos++]);
    TEST_ASSERT_EQUAL_HEX8(0x03, buf[pos++]);  // 3 variant types
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // TypeTraits
    RUN_TEST(test_type_traits_struct_type_id);
    RUN_TEST(test_type_traits_struct_size);
    RUN_TEST(test_type_traits_basic_unchanged);
    RUN_TEST(test_type_traits_containers_unchanged);

    // encodeBasic/decodeBasic for OBJECT
    RUN_TEST(test_encode_basic_object);
    RUN_TEST(test_encode_basic_object_color);

    // ARRAY of OBJECT
    RUN_TEST(test_array_of_object_basic);
    RUN_TEST(test_array_of_object_access);
    RUN_TEST(test_array_of_object_encode_decode);
    RUN_TEST(test_array_of_object_small_struct);
    RUN_TEST(test_array_of_object_single_element);

    // LIST of OBJECT
    RUN_TEST(test_list_of_object_basic);
    RUN_TEST(test_list_of_object_push);
    RUN_TEST(test_list_of_object_push_to_capacity);
    RUN_TEST(test_list_of_object_pop);
    RUN_TEST(test_list_of_object_encode_decode);
    RUN_TEST(test_list_of_object_encode_empty);
    RUN_TEST(test_list_of_object_decode_truncates_at_capacity);
    RUN_TEST(test_list_of_object_setdata);

    // Nested: OBJECT containing ARRAY
    RUN_TEST(test_object_with_array_field_encode_decode);
    RUN_TEST(test_array_of_object_with_array_field);
    RUN_TEST(test_list_of_object_with_array_field);

    // Mixed types
    RUN_TEST(test_list_of_mixed_type_object);

    // Real-world: Segment structs
    RUN_TEST(test_list_of_segments_encode_decode);
    RUN_TEST(test_list_of_segments_max_capacity);

    // Schema
    RUN_TEST(test_schema_array_of_object);
    RUN_TEST(test_schema_list_of_object);
    RUN_TEST(test_schema_list_of_segment);

    // Edge cases
    RUN_TEST(test_encode_decode_object_all_zeros);
    RUN_TEST(test_encode_decode_object_negative_values);
    RUN_TEST(test_decode_truncated_buffer_fails);
    RUN_TEST(test_decode_empty_buffer_fails);
    RUN_TEST(test_list_of_bool_struct);
    RUN_TEST(test_object_property_decode_within_buffer_limit);

    // MicroVariant
    RUN_TEST(test_micro_variant_type_traits);
    RUN_TEST(test_micro_variant_is_wire_safe);
    RUN_TEST(test_micro_variant_not_struct);
    RUN_TEST(test_micro_variant_set_get);
    RUN_TEST(test_micro_variant_encode_decode_basic);
    RUN_TEST(test_object_with_variant_field);
    RUN_TEST(test_array_of_variant);
    RUN_TEST(test_list_of_variant);
    RUN_TEST(test_variant_types_registered);
    RUN_TEST(test_schema_variant_in_object);

    return UNITY_END();
}

#endif // NATIVE_TEST
