#include <unity.h>
#include <array>
#include "ArrayProperty.h"
#include "ListProperty.h"
#include "Property.h"
#include "MicroList.h"
#include "Field.h"
#include "wire/Buffer.h"
#include "wire/TypeCodec.h"

using namespace MicroProto;

// Reset property registry before each test
void setUp() {
    PropertyBase::byId.fill(nullptr);
    PropertyBase::count = 0;
}

void tearDown() {}

// ============== ArrayProperty Tests ==============

void test_array_property_basic() {
    ArrayProperty<uint8_t, 3> rgb("rgb", {255, 128, 64}, PropertyLevel::LOCAL);

    TEST_ASSERT_EQUAL(255, rgb[0]);
    TEST_ASSERT_EQUAL(128, rgb[1]);
    TEST_ASSERT_EQUAL(64, rgb[2]);
    TEST_ASSERT_EQUAL(3, rgb.size());
}

void test_array_property_type_info() {
    ArrayProperty<uint8_t, 3> rgb("rgb", {0, 0, 0}, PropertyLevel::LOCAL);

    TEST_ASSERT_EQUAL(TYPE_ARRAY, rgb.getTypeId());
    TEST_ASSERT_EQUAL(TYPE_UINT8, rgb.getElementTypeId());
    TEST_ASSERT_EQUAL(3, rgb.getElementCount());
    TEST_ASSERT_EQUAL(1, rgb.getElementSize());
    TEST_ASSERT_EQUAL(3, rgb.getSize());  // 3 * 1 byte
    TEST_ASSERT_TRUE(rgb.isContainer());
}

void test_array_property_set_element() {
    ArrayProperty<uint8_t, 3> rgb("rgb", {0, 0, 0}, PropertyLevel::LOCAL);

    rgb.set(0, 255);
    rgb.set(1, 128);
    rgb.set(2, 64);

    TEST_ASSERT_EQUAL(255, rgb[0]);
    TEST_ASSERT_EQUAL(128, rgb[1]);
    TEST_ASSERT_EQUAL(64, rgb[2]);
}

void test_array_property_set_all() {
    ArrayProperty<uint8_t, 3> rgb("rgb", {0, 0, 0}, PropertyLevel::LOCAL);

    std::array<uint8_t, 3> newValue = {100, 200, 50};
    rgb = newValue;

    TEST_ASSERT_EQUAL(100, rgb[0]);
    TEST_ASSERT_EQUAL(200, rgb[1]);
    TEST_ASSERT_EQUAL(50, rgb[2]);
}

void test_array_property_int32() {
    ArrayProperty<int32_t, 2> coords("coords", {100, 200}, PropertyLevel::LOCAL);

    TEST_ASSERT_EQUAL(TYPE_ARRAY, coords.getTypeId());
    TEST_ASSERT_EQUAL(TYPE_INT32, coords.getElementTypeId());
    TEST_ASSERT_EQUAL(2, coords.getElementCount());
    TEST_ASSERT_EQUAL(4, coords.getElementSize());
    TEST_ASSERT_EQUAL(8, coords.getSize());  // 2 * 4 bytes

    TEST_ASSERT_EQUAL(100, coords[0]);
    TEST_ASSERT_EQUAL(200, coords[1]);
}

void test_array_property_iterator() {
    ArrayProperty<uint8_t, 4> arr("arr", {1, 2, 3, 4}, PropertyLevel::LOCAL);

    uint8_t sum = 0;
    for (auto v : arr) {
        sum += v;
    }
    TEST_ASSERT_EQUAL(10, sum);
}

// ============== ListProperty Tests ==============

void test_list_property_empty() {
    ListProperty<uint8_t, 32> list("list", PropertyLevel::LOCAL);

    TEST_ASSERT_EQUAL(0, list.count());
    TEST_ASSERT_TRUE(list.empty());
    TEST_ASSERT_FALSE(list.full());
    TEST_ASSERT_EQUAL(32, list.capacity());
}

void test_list_property_type_info() {
    ListProperty<uint8_t, 32> list("list", PropertyLevel::LOCAL);

    TEST_ASSERT_EQUAL(TYPE_LIST, list.getTypeId());
    TEST_ASSERT_EQUAL(TYPE_UINT8, list.getElementTypeId());
    TEST_ASSERT_EQUAL(1, list.getElementSize());
    TEST_ASSERT_EQUAL(32, list.getMaxElementCount());
    TEST_ASSERT_TRUE(list.isContainer());
}

void test_list_property_push_pop() {
    ListProperty<int32_t, 8> list("list", PropertyLevel::LOCAL);

    list.push(100);
    list.push(200);
    list.push(300);

    TEST_ASSERT_EQUAL(3, list.count());
    TEST_ASSERT_EQUAL(100, list[0]);
    TEST_ASSERT_EQUAL(200, list[1]);
    TEST_ASSERT_EQUAL(300, list[2]);

    list.pop();
    TEST_ASSERT_EQUAL(2, list.count());
    TEST_ASSERT_EQUAL(200, list[1]);
}

void test_list_property_initializer() {
    ListProperty<uint8_t, 16> list("list", {10, 20, 30, 40}, PropertyLevel::LOCAL);

    TEST_ASSERT_EQUAL(4, list.count());
    TEST_ASSERT_EQUAL(10, list[0]);
    TEST_ASSERT_EQUAL(40, list[3]);
}

void test_list_property_string() {
    StringProperty<64> str("name", "hello", PropertyLevel::LOCAL);

    TEST_ASSERT_EQUAL(5, str.count());
    TEST_ASSERT_EQUAL('h', str[0]);
    TEST_ASSERT_EQUAL('o', str[4]);

    char buf[32];
    str.getString(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("hello", buf);
}

void test_list_property_set_string() {
    StringProperty<64> str("name", PropertyLevel::LOCAL);

    str.setString("world");
    TEST_ASSERT_EQUAL(5, str.count());

    char buf[32];
    str.getString(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("world", buf);
}

void test_list_property_clear() {
    ListProperty<uint8_t, 16> list("list", {1, 2, 3}, PropertyLevel::LOCAL);

    TEST_ASSERT_EQUAL(3, list.count());
    list.clear();
    TEST_ASSERT_EQUAL(0, list.count());
    TEST_ASSERT_TRUE(list.empty());
}

void test_list_property_resize() {
    ListProperty<uint8_t, 16> list("list", PropertyLevel::LOCAL);

    list.resize(5);
    TEST_ASSERT_EQUAL(5, list.count());
    TEST_ASSERT_EQUAL(0, list[0]);  // Default initialized
}

void test_list_property_iterator() {
    ListProperty<int32_t, 8> list("list", {10, 20, 30}, PropertyLevel::LOCAL);

    int32_t sum = 0;
    for (auto v : list) {
        sum += v;
    }
    TEST_ASSERT_EQUAL(60, sum);
}

// ============== Wire Format Tests ==============

void test_array_encode() {
    ArrayProperty<uint8_t, 3> rgb("rgb", {255, 128, 64}, PropertyLevel::LOCAL);

    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));

    TEST_ASSERT_TRUE(TypeCodec::encodeProperty(wb, &rgb));
    TEST_ASSERT_EQUAL(3, wb.position());  // 3 bytes, no length prefix

    TEST_ASSERT_EQUAL(255, buf[0]);
    TEST_ASSERT_EQUAL(128, buf[1]);
    TEST_ASSERT_EQUAL(64, buf[2]);
}

void test_array_encode_int32() {
    ArrayProperty<int32_t, 2> arr("arr", {0x12345678, static_cast<int32_t>(0xABCDEF01)}, PropertyLevel::LOCAL);

    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));

    TEST_ASSERT_TRUE(TypeCodec::encodeProperty(wb, &arr));
    TEST_ASSERT_EQUAL(8, wb.position());  // 2 * 4 bytes

    // Little-endian
    TEST_ASSERT_EQUAL(0x78, buf[0]);
    TEST_ASSERT_EQUAL(0x56, buf[1]);
    TEST_ASSERT_EQUAL(0x34, buf[2]);
    TEST_ASSERT_EQUAL(0x12, buf[3]);
}

void test_list_encode() {
    ListProperty<uint8_t, 16> list("list", {10, 20, 30}, PropertyLevel::LOCAL);

    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));

    TEST_ASSERT_TRUE(TypeCodec::encodeProperty(wb, &list));
    TEST_ASSERT_EQUAL(4, wb.position());  // varint(3) + 3 bytes

    TEST_ASSERT_EQUAL(3, buf[0]);   // Count
    TEST_ASSERT_EQUAL(10, buf[1]);
    TEST_ASSERT_EQUAL(20, buf[2]);
    TEST_ASSERT_EQUAL(30, buf[3]);
}

void test_list_encode_empty() {
    ListProperty<uint8_t, 16> list("list", PropertyLevel::LOCAL);

    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));

    TEST_ASSERT_TRUE(TypeCodec::encodeProperty(wb, &list));
    TEST_ASSERT_EQUAL(1, wb.position());  // Just varint(0)
    TEST_ASSERT_EQUAL(0, buf[0]);
}

void test_list_encode_string() {
    StringProperty<64> str("name", "test", PropertyLevel::LOCAL);

    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));

    TEST_ASSERT_TRUE(TypeCodec::encodeProperty(wb, &str));
    TEST_ASSERT_EQUAL(5, wb.position());  // varint(4) + "test"

    TEST_ASSERT_EQUAL(4, buf[0]);
    TEST_ASSERT_EQUAL('t', buf[1]);
    TEST_ASSERT_EQUAL('e', buf[2]);
    TEST_ASSERT_EQUAL('s', buf[3]);
    TEST_ASSERT_EQUAL('t', buf[4]);
}

void test_array_decode() {
    ArrayProperty<uint8_t, 3> rgb("rgb", {0, 0, 0}, PropertyLevel::LOCAL);

    uint8_t data[] = {100, 150, 200};
    ReadBuffer rb(data, sizeof(data));

    TEST_ASSERT_TRUE(TypeCodec::decodeProperty(rb, &rgb));

    TEST_ASSERT_EQUAL(100, rgb[0]);
    TEST_ASSERT_EQUAL(150, rgb[1]);
    TEST_ASSERT_EQUAL(200, rgb[2]);
}

void test_list_decode() {
    ListProperty<uint8_t, 16> list("list", PropertyLevel::LOCAL);

    uint8_t data[] = {3, 10, 20, 30};  // count=3, then 3 bytes
    ReadBuffer rb(data, sizeof(data));

    TEST_ASSERT_TRUE(TypeCodec::decodeProperty(rb, &list));

    TEST_ASSERT_EQUAL(3, list.count());
    TEST_ASSERT_EQUAL(10, list[0]);
    TEST_ASSERT_EQUAL(20, list[1]);
    TEST_ASSERT_EQUAL(30, list[2]);
}

void test_list_decode_truncate() {
    ListProperty<uint8_t, 4> list("list", PropertyLevel::LOCAL);  // Max 4 elements

    uint8_t data[] = {6, 1, 2, 3, 4, 5, 6};  // count=6, but max is 4
    ReadBuffer rb(data, sizeof(data));

    TEST_ASSERT_TRUE(TypeCodec::decodeProperty(rb, &list));

    TEST_ASSERT_EQUAL(4, list.count());  // Truncated to max
    TEST_ASSERT_EQUAL(1, list[0]);
    TEST_ASSERT_EQUAL(4, list[3]);
}

// ============== Property<MicroList<...>> Tests ==============

void test_property_microlist_empty() {
    Property<MicroList<uint8_t, 4, 16>> list("list", PropertyLevel::LOCAL);

    TEST_ASSERT_EQUAL(0, list.size());
    TEST_ASSERT_TRUE(list.empty());
    TEST_ASSERT_EQUAL(TYPE_LIST, list.getTypeId());
    TEST_ASSERT_TRUE(list.isContainer());
}

void test_property_microlist_initializer() {
    Property<MicroList<uint8_t, 4, 16>> list("list", {10, 20, 30}, PropertyLevel::LOCAL);

    TEST_ASSERT_EQUAL(3, list.size());
    TEST_ASSERT_EQUAL(10, list[0]);
    TEST_ASSERT_EQUAL(20, list[1]);
    TEST_ASSERT_EQUAL(30, list[2]);
}

void test_property_microlist_push_pop() {
    Property<MicroList<int32_t, 4, 8>> list("list", PropertyLevel::LOCAL);

    list.push_back(100);
    list.push_back(200);
    TEST_ASSERT_EQUAL(2, list.size());
    TEST_ASSERT_EQUAL(100, list[0]);
    TEST_ASSERT_EQUAL(200, list[1]);

    list.pop_back();
    TEST_ASSERT_EQUAL(1, list.size());
    TEST_ASSERT_EQUAL(100, list[0]);
}

void test_property_microlist_type_info() {
    Property<MicroList<int32_t, 4, 8>> list("list", {1, 2, 3}, PropertyLevel::LOCAL);

    TEST_ASSERT_EQUAL(TYPE_LIST, list.getTypeId());
    TEST_ASSERT_EQUAL(TYPE_INT32, list.getElementTypeId());
    TEST_ASSERT_EQUAL(4, list.getElementSize());
    TEST_ASSERT_EQUAL(3, list.getElementCount());
    TEST_ASSERT_EQUAL(8, list.getMaxElementCount());
    TEST_ASSERT_EQUAL(12, list.getSize());  // 3 * 4 bytes
}

void test_property_microlist_clear() {
    Property<MicroList<uint8_t, 4, 16>> list("list", {1, 2, 3, 4, 5}, PropertyLevel::LOCAL);

    TEST_ASSERT_EQUAL(5, list.size());
    list.clear();
    TEST_ASSERT_EQUAL(0, list.size());
    TEST_ASSERT_TRUE(list.empty());
}

void test_property_microlist_resize() {
    Property<MicroList<uint8_t, 4, 16>> list("list", {1, 2}, PropertyLevel::LOCAL);

    list.resize(5);
    TEST_ASSERT_EQUAL(5, list.size());
    TEST_ASSERT_EQUAL(1, list[0]);
    TEST_ASSERT_EQUAL(2, list[1]);
    TEST_ASSERT_EQUAL(0, list[2]);  // Default value

    list.resize(1);
    TEST_ASSERT_EQUAL(1, list.size());
    TEST_ASSERT_EQUAL(1, list[0]);
}

void test_property_microlist_set_element() {
    Property<MicroList<uint8_t, 4, 16>> list("list", {1, 2, 3}, PropertyLevel::LOCAL);

    list.set(1, 99);
    TEST_ASSERT_EQUAL(99, list[1]);
}

void test_property_microlist_iterator() {
    Property<MicroList<uint8_t, 4, 16>> list("list", {10, 20, 30, 40}, PropertyLevel::LOCAL);

    uint8_t sum = 0;
    for (const auto& val : list) {
        sum += val;
    }
    TEST_ASSERT_EQUAL(100, sum);
}

void test_property_microlist_encode() {
    Property<MicroList<uint8_t, 4, 16>> list("list", {10, 20, 30}, PropertyLevel::LOCAL);

    uint8_t buffer[64];
    WriteBuffer wb(buffer, sizeof(buffer));

    TEST_ASSERT_TRUE(TypeCodec::encodeProperty(wb, &list));

    // Format: varint(3) + 10 + 20 + 30
    TEST_ASSERT_EQUAL(4, wb.position());
    TEST_ASSERT_EQUAL(3, buffer[0]);  // count
    TEST_ASSERT_EQUAL(10, buffer[1]);
    TEST_ASSERT_EQUAL(20, buffer[2]);
    TEST_ASSERT_EQUAL(30, buffer[3]);
}

void test_property_microlist_decode() {
    Property<MicroList<uint8_t, 4, 16>> list("list", PropertyLevel::LOCAL);

    uint8_t data[] = {4, 5, 10, 15, 20};  // count=4, then 4 bytes
    ReadBuffer rb(data, sizeof(data));

    TEST_ASSERT_TRUE(TypeCodec::decodeProperty(rb, &list));

    TEST_ASSERT_EQUAL(4, list.size());
    TEST_ASSERT_EQUAL(5, list[0]);
    TEST_ASSERT_EQUAL(10, list[1]);
    TEST_ASSERT_EQUAL(15, list[2]);
    TEST_ASSERT_EQUAL(20, list[3]);
}

void test_property_microlist_encode_int32() {
    Property<MicroList<int32_t, 4, 8>> list("list", {1000, -500}, PropertyLevel::LOCAL);

    uint8_t buffer[64];
    WriteBuffer wb(buffer, sizeof(buffer));

    TEST_ASSERT_TRUE(TypeCodec::encodeProperty(wb, &list));

    // Format: varint(2) + int32(1000) + int32(-500)
    TEST_ASSERT_EQUAL(9, wb.position());  // 1 + 4 + 4
}

void test_property_microlist_decode_int32() {
    Property<MicroList<int32_t, 4, 8>> list("list", PropertyLevel::LOCAL);

    // count=2, then two little-endian int32s: 1000, -1
    uint8_t data[] = {
        2,                      // count
        0xE8, 0x03, 0x00, 0x00, // 1000 in little-endian
        0xFF, 0xFF, 0xFF, 0xFF  // -1 in little-endian
    };
    ReadBuffer rb(data, sizeof(data));

    TEST_ASSERT_TRUE(TypeCodec::decodeProperty(rb, &list));

    TEST_ASSERT_EQUAL(2, list.size());
    TEST_ASSERT_EQUAL(1000, list[0]);
    TEST_ASSERT_EQUAL(-1, list[1]);
}

void test_microlist_typecodec_encode() {
    MicroList<uint8_t, 4, 16> list = {1, 2, 3, 4, 5};

    uint8_t buffer[64];
    WriteBuffer wb(buffer, sizeof(buffer));

    TEST_ASSERT_TRUE(TypeCodec::encode(wb, list));

    TEST_ASSERT_EQUAL(6, wb.position());  // 1 (count) + 5 (elements)
    TEST_ASSERT_EQUAL(5, buffer[0]);
    TEST_ASSERT_EQUAL(1, buffer[1]);
    TEST_ASSERT_EQUAL(5, buffer[5]);
}

void test_microlist_typecodec_decode() {
    MicroList<uint8_t, 4, 16> list;

    uint8_t data[] = {3, 100, 200, 255};
    ReadBuffer rb(data, sizeof(data));

    TEST_ASSERT_TRUE(TypeCodec::decode(rb, list));

    TEST_ASSERT_EQUAL(3, list.size());
    TEST_ASSERT_EQUAL(100, list[0]);
    TEST_ASSERT_EQUAL(200, list[1]);
    TEST_ASSERT_EQUAL(255, list[2]);
}

// ============== Nested Container Tests ==============

// Test type traits for nested containers
void test_nested_type_traits() {
    // Basic types are valid
    static_assert(is_microproto_type_v<uint8_t>, "uint8_t should be valid");
    static_assert(is_microproto_type_v<int32_t>, "int32_t should be valid");

    // Single-level containers are valid
    static_assert(is_microproto_type_v<std::array<uint8_t, 3>>, "array<uint8_t> should be valid");
    static_assert(is_microproto_type_v<MicroList<uint8_t, 4, 8>>, "MicroList<uint8_t> should be valid");

    // Nested containers are valid
    static_assert(is_microproto_type_v<MicroList<std::array<uint8_t, 3>, 4, 8>>,
        "MicroList<array> should be valid");
    static_assert(is_microproto_type_v<std::array<MicroList<uint8_t, 4, 8>, 3>>,
        "array<MicroList> should be valid");
    static_assert(is_microproto_type_v<MicroList<MicroList<uint8_t, 2, 4>, 2, 4>>,
        "MicroList<MicroList> should be valid");

    // Fixed-size checks
    static_assert(is_microproto_fixed_size_v<uint8_t>, "uint8_t is fixed");
    static_assert(is_microproto_fixed_size_v<std::array<uint8_t, 3>>, "array<uint8_t> is fixed");
    static_assert(!is_microproto_fixed_size_v<MicroList<uint8_t, 4, 8>>, "MicroList is NOT fixed");
    static_assert(!is_microproto_fixed_size_v<std::array<MicroList<uint8_t, 4, 8>, 3>>,
        "array<MicroList> is NOT fixed");

    // Structs
    struct Point { int32_t x, y; };
    static_assert(is_microproto_type_v<Point>, "POD struct should be valid");
    static_assert(is_microproto_struct_v<Point>, "POD struct should be struct type");
    static_assert(is_microproto_fixed_size_v<Point>, "POD struct is fixed");

    TEST_PASS();
}

// Test encoding MicroList of std::array (list of RGB colors)
void test_nested_list_of_arrays_encode() {
    MicroList<std::array<uint8_t, 3>, 4, 8> colors;
    colors.push_back({255, 0, 0});    // Red
    colors.push_back({0, 255, 0});    // Green
    colors.push_back({0, 0, 255});    // Blue

    uint8_t buffer[64];
    WriteBuffer wb(buffer, sizeof(buffer));

    TEST_ASSERT_TRUE(TypeCodec::encode(wb, colors));

    // Format: varint(3) + 3 * (3 bytes each) = 1 + 9 = 10 bytes
    TEST_ASSERT_EQUAL(10, wb.position());
    TEST_ASSERT_EQUAL(3, buffer[0]);  // count
    TEST_ASSERT_EQUAL(255, buffer[1]); // red R
    TEST_ASSERT_EQUAL(0, buffer[4]);   // green R
    TEST_ASSERT_EQUAL(0, buffer[7]);   // blue R
    TEST_ASSERT_EQUAL(255, buffer[9]); // blue B
}

void test_nested_list_of_arrays_decode() {
    uint8_t data[] = {
        2,              // count = 2
        10, 20, 30,     // array 1
        40, 50, 60      // array 2
    };
    ReadBuffer rb(data, sizeof(data));

    MicroList<std::array<uint8_t, 3>, 4, 8> colors;
    TEST_ASSERT_TRUE(TypeCodec::decode(rb, colors));

    TEST_ASSERT_EQUAL(2, colors.size());
    TEST_ASSERT_EQUAL(10, colors[0][0]);
    TEST_ASSERT_EQUAL(30, colors[0][2]);
    TEST_ASSERT_EQUAL(40, colors[1][0]);
    TEST_ASSERT_EQUAL(60, colors[1][2]);
}

// Test encoding std::array of MicroList
void test_nested_array_of_lists_encode() {
    std::array<MicroList<uint8_t, 4, 8>, 2> lists;
    lists[0] = {1, 2, 3};
    lists[1] = {10, 20};

    uint8_t buffer[64];
    WriteBuffer wb(buffer, sizeof(buffer));

    TEST_ASSERT_TRUE(TypeCodec::encode(wb, lists));

    // Format: list1(varint(3) + 1,2,3) + list2(varint(2) + 10,20)
    // = (1 + 3) + (1 + 2) = 7 bytes
    TEST_ASSERT_EQUAL(7, wb.position());
    TEST_ASSERT_EQUAL(3, buffer[0]);   // list1 count
    TEST_ASSERT_EQUAL(1, buffer[1]);   // list1[0]
    TEST_ASSERT_EQUAL(2, buffer[4]);   // list2 count
    TEST_ASSERT_EQUAL(10, buffer[5]);  // list2[0]
}

void test_nested_array_of_lists_decode() {
    uint8_t data[] = {
        3, 100, 101, 102,  // list 1: count=3
        2, 200, 201        // list 2: count=2
    };
    ReadBuffer rb(data, sizeof(data));

    std::array<MicroList<uint8_t, 4, 8>, 2> lists;
    TEST_ASSERT_TRUE(TypeCodec::decode(rb, lists));

    TEST_ASSERT_EQUAL(3, lists[0].size());
    TEST_ASSERT_EQUAL(100, lists[0][0]);
    TEST_ASSERT_EQUAL(102, lists[0][2]);
    TEST_ASSERT_EQUAL(2, lists[1].size());
    TEST_ASSERT_EQUAL(200, lists[1][0]);
}

// Test MicroList of MicroList (2D dynamic array)
void test_nested_list_of_lists_encode() {
    MicroList<MicroList<uint8_t, 4, 8>, 2, 4> matrix;
    matrix.push_back({1, 2, 3});
    matrix.push_back({10, 20});

    uint8_t buffer[64];
    WriteBuffer wb(buffer, sizeof(buffer));

    TEST_ASSERT_TRUE(TypeCodec::encode(wb, matrix));

    // Format: varint(2) + row1(varint(3)+1,2,3) + row2(varint(2)+10,20)
    // = 1 + 4 + 3 = 8 bytes
    TEST_ASSERT_EQUAL(8, wb.position());
    TEST_ASSERT_EQUAL(2, buffer[0]);  // outer count
    TEST_ASSERT_EQUAL(3, buffer[1]);  // row1 count
    TEST_ASSERT_EQUAL(1, buffer[2]);  // row1[0]
    TEST_ASSERT_EQUAL(2, buffer[5]);  // row2 count
}

void test_nested_list_of_lists_decode() {
    uint8_t data[] = {
        2,              // outer count = 2
        3, 5, 6, 7,     // inner list 1: count=3
        2, 8, 9         // inner list 2: count=2
    };
    ReadBuffer rb(data, sizeof(data));

    MicroList<MicroList<uint8_t, 4, 8>, 2, 4> matrix;
    TEST_ASSERT_TRUE(TypeCodec::decode(rb, matrix));

    TEST_ASSERT_EQUAL(2, matrix.size());
    TEST_ASSERT_EQUAL(3, matrix[0].size());
    TEST_ASSERT_EQUAL(5, matrix[0][0]);
    TEST_ASSERT_EQUAL(7, matrix[0][2]);
    TEST_ASSERT_EQUAL(2, matrix[1].size());
    TEST_ASSERT_EQUAL(8, matrix[1][0]);
}

// Test deeply nested: list of arrays of lists
void test_deeply_nested_encode_decode() {
    // MicroList<std::array<MicroList<uint8_t>>> - 3 levels deep
    using InnerList = MicroList<uint8_t, 2, 4>;
    using MiddleArray = std::array<InnerList, 2>;
    using OuterList = MicroList<MiddleArray, 2, 4>;

    OuterList data;
    MiddleArray arr1;
    arr1[0] = {1, 2};
    arr1[1] = {3, 4, 5};
    data.push_back(arr1);

    uint8_t buffer[64];
    WriteBuffer wb(buffer, sizeof(buffer));
    TEST_ASSERT_TRUE(TypeCodec::encode(wb, data));

    // Decode back
    ReadBuffer rb(buffer, wb.position());
    OuterList decoded;
    TEST_ASSERT_TRUE(TypeCodec::decode(rb, decoded));

    TEST_ASSERT_EQUAL(1, decoded.size());
    TEST_ASSERT_EQUAL(2, decoded[0][0].size());
    TEST_ASSERT_EQUAL(1, decoded[0][0][0]);
    TEST_ASSERT_EQUAL(2, decoded[0][0][1]);
    TEST_ASSERT_EQUAL(3, decoded[0][1].size());
    TEST_ASSERT_EQUAL(5, decoded[0][1][2]);
}

// Test struct encoding with nested containers
void test_struct_encode_decode() {
    struct Point { int32_t x, y; };

    MicroList<Point, 4, 8> points;
    points.push_back({100, 200});
    points.push_back({-50, 300});

    uint8_t buffer[64];
    WriteBuffer wb(buffer, sizeof(buffer));
    TEST_ASSERT_TRUE(TypeCodec::encode(wb, points));

    // Format: varint(2) + 2 * (4 + 4 bytes) = 1 + 16 = 17 bytes
    TEST_ASSERT_EQUAL(17, wb.position());

    ReadBuffer rb(buffer, wb.position());
    MicroList<Point, 4, 8> decoded;
    TEST_ASSERT_TRUE(TypeCodec::decode(rb, decoded));

    TEST_ASSERT_EQUAL(2, decoded.size());
    TEST_ASSERT_EQUAL(100, decoded[0].x);
    TEST_ASSERT_EQUAL(200, decoded[0].y);
    TEST_ASSERT_EQUAL(-50, decoded[1].x);
}

// Test array of structs
void test_array_of_structs_encode_decode() {
    struct RGB { uint8_t r, g, b; };

    std::array<RGB, 3> colors = {{{255, 0, 0}, {0, 255, 0}, {0, 0, 255}}};

    uint8_t buffer[64];
    WriteBuffer wb(buffer, sizeof(buffer));
    TEST_ASSERT_TRUE(TypeCodec::encode(wb, colors));

    // Fixed size: 3 * 3 = 9 bytes
    TEST_ASSERT_EQUAL(9, wb.position());

    ReadBuffer rb(buffer, wb.position());
    std::array<RGB, 3> decoded;
    TEST_ASSERT_TRUE(TypeCodec::decode(rb, decoded));

    TEST_ASSERT_EQUAL(255, decoded[0].r);
    TEST_ASSERT_EQUAL(0, decoded[1].r);
    TEST_ASSERT_EQUAL(255, decoded[2].b);
}

// ============== std::string in containers ==============

void test_string_type_traits() {
    // std::string is NOT trivially copyable
    static_assert(!std::is_trivially_copyable_v<std::string>,
        "std::string should NOT be trivially copyable");

    // std::string IS a microproto string type
    static_assert(is_microproto_string_v<std::string>,
        "std::string should be a microproto string");

    // std::string is NOT a basic type, struct, or container
    static_assert(!is_microproto_basic_type_v<std::string>,
        "std::string should NOT be a basic type");
    static_assert(!is_microproto_struct_v<std::string>,
        "std::string should NOT be a struct");
    static_assert(!is_microproto_container_v<std::string>,
        "std::string should NOT be a container");

    // But std::string IS a valid microproto type (via string trait)
    static_assert(is_microproto_type_v<std::string>,
        "std::string should be a valid microproto type");

    // MicroList<std::string> is valid
    static_assert(is_microproto_type_v<MicroList<std::string, 4, 8>>,
        "MicroList<std::string> should be valid");

    TEST_PASS();
}

void test_string_encode_decode() {
    std::string str = "Hello, World!";

    uint8_t buffer[64];
    WriteBuffer wb(buffer, sizeof(buffer));

    TEST_ASSERT_TRUE(TypeCodec::encode(wb, str));

    // Format: varint(13) + "Hello, World!"
    TEST_ASSERT_EQUAL(14, wb.position());  // 1 + 13
    TEST_ASSERT_EQUAL(13, buffer[0]);      // length
    TEST_ASSERT_EQUAL('H', buffer[1]);
    TEST_ASSERT_EQUAL('!', buffer[13]);

    // Decode
    ReadBuffer rb(buffer, wb.position());
    std::string decoded;
    TEST_ASSERT_TRUE(TypeCodec::decode(rb, decoded));
    TEST_ASSERT_TRUE(decoded == "Hello, World!");
}

void test_string_empty_encode_decode() {
    std::string str = "";

    uint8_t buffer[64];
    WriteBuffer wb(buffer, sizeof(buffer));

    TEST_ASSERT_TRUE(TypeCodec::encode(wb, str));
    TEST_ASSERT_EQUAL(1, wb.position());  // just varint(0)
    TEST_ASSERT_EQUAL(0, buffer[0]);

    ReadBuffer rb(buffer, wb.position());
    std::string decoded = "not empty";
    TEST_ASSERT_TRUE(TypeCodec::decode(rb, decoded));
    TEST_ASSERT_TRUE(decoded.empty());
}

void test_microlist_of_strings_encode_decode() {
    MicroList<std::string, 4, 8> list;
    list.push_back("one");
    list.push_back("two");
    list.push_back("three");

    uint8_t buffer[64];
    WriteBuffer wb(buffer, sizeof(buffer));

    TEST_ASSERT_TRUE(TypeCodec::encode(wb, list));

    // Format: varint(3) + string("one") + string("two") + string("three")
    // = 1 + (1+3) + (1+3) + (1+5) = 1 + 4 + 4 + 6 = 15 bytes
    TEST_ASSERT_EQUAL(15, wb.position());

    // Decode
    ReadBuffer rb(buffer, wb.position());
    MicroList<std::string, 4, 8> decoded;
    TEST_ASSERT_TRUE(TypeCodec::decode(rb, decoded));

    TEST_ASSERT_EQUAL(3, decoded.size());
    TEST_ASSERT_TRUE(decoded[0] == "one");
    TEST_ASSERT_TRUE(decoded[1] == "two");
    TEST_ASSERT_TRUE(decoded[2] == "three");
}

void test_array_of_strings_encode_decode() {
    std::array<std::string, 2> arr = {"hello", "world"};

    uint8_t buffer[64];
    WriteBuffer wb(buffer, sizeof(buffer));

    TEST_ASSERT_TRUE(TypeCodec::encode(wb, arr));

    // Format: string("hello") + string("world")
    // = (1+5) + (1+5) = 12 bytes
    TEST_ASSERT_EQUAL(12, wb.position());

    // Decode
    ReadBuffer rb(buffer, wb.position());
    std::array<std::string, 2> decoded;
    TEST_ASSERT_TRUE(TypeCodec::decode(rb, decoded));

    TEST_ASSERT_TRUE(decoded[0] == "hello");
    TEST_ASSERT_TRUE(decoded[1] == "world");
}

void test_nested_string_containers() {
    // MicroList of arrays of strings
    MicroList<std::array<std::string, 2>, 2, 4> nested;
    nested.push_back({"a", "b"});
    nested.push_back({"x", "y"});

    uint8_t buffer[128];
    WriteBuffer wb(buffer, sizeof(buffer));

    TEST_ASSERT_TRUE(TypeCodec::encode(wb, nested));

    // Decode
    ReadBuffer rb(buffer, wb.position());
    MicroList<std::array<std::string, 2>, 2, 4> decoded;
    TEST_ASSERT_TRUE(TypeCodec::decode(rb, decoded));

    TEST_ASSERT_EQUAL(2, decoded.size());
    TEST_ASSERT_TRUE(decoded[0][0] == "a");
    TEST_ASSERT_TRUE(decoded[0][1] == "b");
    TEST_ASSERT_TRUE(decoded[1][0] == "x");
    TEST_ASSERT_TRUE(decoded[1][1] == "y");
}

// ============== Schema Type Encoding Tests ==============

void test_schema_basic_type() {
    // Test encoding basic type (uint8_t)
    uint8_t buffer[32];
    WriteBuffer wb(buffer, sizeof(buffer));

    TEST_ASSERT_TRUE(SchemaTypeEncoder::encode<uint8_t>(wb, nullptr));

    // Should be: TYPE_UINT8 + 0 (no constraints)
    TEST_ASSERT_EQUAL(2, wb.position());
    TEST_ASSERT_EQUAL(TYPE_UINT8, buffer[0]);
    TEST_ASSERT_EQUAL(0, buffer[1]);  // No constraints
}

void test_schema_basic_type_with_constraints() {
    // Test encoding basic type with constraints
    uint8_t buffer[32];
    WriteBuffer wb(buffer, sizeof(buffer));

    ValueConstraints constraints;
    constraints.setMin<uint8_t>(0);
    constraints.setMax<uint8_t>(100);

    TEST_ASSERT_TRUE(SchemaTypeEncoder::encode<uint8_t>(wb, &constraints));

    // Should be: TYPE_UINT8 + flags + min + max
    TEST_ASSERT_EQUAL(4, wb.position());
    TEST_ASSERT_EQUAL(TYPE_UINT8, buffer[0]);
    TEST_ASSERT_EQUAL(0x03, buffer[1]);  // has_min | has_max
    TEST_ASSERT_EQUAL(0, buffer[2]);     // min
    TEST_ASSERT_EQUAL(100, buffer[3]);   // max
}

void test_schema_array_of_basic() {
    // Test encoding std::array<uint8_t, 3>
    uint8_t buffer[32];
    WriteBuffer wb(buffer, sizeof(buffer));

    TEST_ASSERT_TRUE((SchemaTypeEncoder::encode<uint8_t, 3>(wb, nullptr)));

    // Should be: TYPE_ARRAY + varint(3) + TYPE_UINT8 + 0
    TEST_ASSERT_EQUAL(4, wb.position());
    TEST_ASSERT_EQUAL(TYPE_ARRAY, buffer[0]);
    TEST_ASSERT_EQUAL(3, buffer[1]);      // element count
    TEST_ASSERT_EQUAL(TYPE_UINT8, buffer[2]);
    TEST_ASSERT_EQUAL(0, buffer[3]);      // no element constraints
}

void test_schema_list_of_basic() {
    // Test encoding MicroList<int32_t, 4, 16>
    uint8_t buffer[32];
    WriteBuffer wb(buffer, sizeof(buffer));

    TEST_ASSERT_TRUE((SchemaTypeEncoder::encode<int32_t, 4, 16>(wb, nullptr, nullptr)));

    // Should be: TYPE_LIST + 0 (no container constraints) + TYPE_INT32 + 0
    TEST_ASSERT_EQUAL(4, wb.position());
    TEST_ASSERT_EQUAL(TYPE_LIST, buffer[0]);
    TEST_ASSERT_EQUAL(0, buffer[1]);      // no container constraints
    TEST_ASSERT_EQUAL(TYPE_INT32, buffer[2]);
    TEST_ASSERT_EQUAL(0, buffer[3]);      // no element constraints
}

void test_schema_nested_array_of_list() {
    // Test encoding std::array<MicroList<uint8_t, 4, 8>, 2>
    // This should recursively encode the nested type
    uint8_t buffer[32];
    WriteBuffer wb(buffer, sizeof(buffer));

    // encode<ElementType, N> for array
    // But we need to encode array of MicroList
    // This requires calling the right overload

    // Actually for nested, we need the recursive template
    // Let's test via SchemaTypeEncoder directly with proper template args

    // For std::array<MicroList<uint8_t, 4, 8>, 2>:
    // TYPE_ARRAY + count(2) + (LIST type definition)
    // Where LIST type definition = TYPE_LIST + 0 + TYPE_UINT8 + 0

    // We use encodeContainerTypeImpl for this
    using ArrayType = std::array<MicroList<uint8_t, 4, 8>, 2>;
    TEST_ASSERT_TRUE(encodeContainerTypeImpl(wb, static_cast<ArrayType*>(nullptr)));

    // Expected: TYPE_ARRAY + 2 + TYPE_LIST + 0 + TYPE_UINT8 + 0
    TEST_ASSERT_EQUAL(6, wb.position());
    TEST_ASSERT_EQUAL(TYPE_ARRAY, buffer[0]);
    TEST_ASSERT_EQUAL(2, buffer[1]);
    TEST_ASSERT_EQUAL(TYPE_LIST, buffer[2]);
    TEST_ASSERT_EQUAL(0, buffer[3]);
    TEST_ASSERT_EQUAL(TYPE_UINT8, buffer[4]);
    TEST_ASSERT_EQUAL(0, buffer[5]);
}

void test_schema_nested_list_of_array() {
    // Test encoding MicroList<std::array<uint8_t, 3>, 2, 4>
    uint8_t buffer[32];
    WriteBuffer wb(buffer, sizeof(buffer));

    using ListType = MicroList<std::array<uint8_t, 3>, 2, 4>;
    TEST_ASSERT_TRUE(encodeContainerTypeImpl(wb, static_cast<ListType*>(nullptr)));

    // Expected: TYPE_LIST + 0 + TYPE_ARRAY + 3 + TYPE_UINT8 + 0
    TEST_ASSERT_EQUAL(6, wb.position());
    TEST_ASSERT_EQUAL(TYPE_LIST, buffer[0]);
    TEST_ASSERT_EQUAL(0, buffer[1]);  // no container constraints
    TEST_ASSERT_EQUAL(TYPE_ARRAY, buffer[2]);
    TEST_ASSERT_EQUAL(3, buffer[3]);  // array size
    TEST_ASSERT_EQUAL(TYPE_UINT8, buffer[4]);
    TEST_ASSERT_EQUAL(0, buffer[5]);  // no element constraints
}

void test_schema_string_as_list() {
    // Test that std::string is encoded as LIST<UINT8>
    uint8_t buffer[32];
    WriteBuffer wb(buffer, sizeof(buffer));

    TEST_ASSERT_TRUE(SchemaTypeEncoder::encode(wb, static_cast<const std::string*>(nullptr), nullptr, nullptr));

    // Expected: TYPE_LIST + 0 + TYPE_UINT8 + 0
    TEST_ASSERT_EQUAL(4, wb.position());
    TEST_ASSERT_EQUAL(TYPE_LIST, buffer[0]);
    TEST_ASSERT_EQUAL(0, buffer[1]);  // no container constraints
    TEST_ASSERT_EQUAL(TYPE_UINT8, buffer[2]);
    TEST_ASSERT_EQUAL(0, buffer[3]);  // no element constraints
}

void test_schema_deeply_nested() {
    // Test encoding MicroList<MicroList<std::array<uint8_t, 2>, 2, 4>, 2, 4>
    // 3 levels deep
    uint8_t buffer[64];
    WriteBuffer wb(buffer, sizeof(buffer));

    using DeepType = MicroList<MicroList<std::array<uint8_t, 2>, 2, 4>, 2, 4>;
    TEST_ASSERT_TRUE(encodeContainerTypeImpl(wb, static_cast<DeepType*>(nullptr)));

    // Expected: TYPE_LIST + 0 + TYPE_LIST + 0 + TYPE_ARRAY + 2 + TYPE_UINT8 + 0
    TEST_ASSERT_EQUAL(8, wb.position());

    size_t pos = 0;
    TEST_ASSERT_EQUAL(TYPE_LIST, buffer[pos++]);   // Outer list
    TEST_ASSERT_EQUAL(0, buffer[pos++]);           // no container constraints
    TEST_ASSERT_EQUAL(TYPE_LIST, buffer[pos++]);   // Inner list
    TEST_ASSERT_EQUAL(0, buffer[pos++]);           // no container constraints
    TEST_ASSERT_EQUAL(TYPE_ARRAY, buffer[pos++]);  // Array
    TEST_ASSERT_EQUAL(2, buffer[pos++]);           // array size
    TEST_ASSERT_EQUAL(TYPE_UINT8, buffer[pos++]);  // element type
    TEST_ASSERT_EQUAL(0, buffer[pos++]);           // no element constraints
}

void test_schema_object_with_fields() {
    // Test encoding a struct as OBJECT with field types
    // Per spec 4.3: OBJECT = type_id(0x22) + varint(field_count) + [ident name + DATA_TYPE_DEFINITION]...
    struct Point { int32_t x, y; };

    uint8_t buffer[64];
    WriteBuffer wb(buffer, sizeof(buffer));

    // Encode Point struct as OBJECT
    TEST_ASSERT_TRUE(SchemaTypeEncoder::encode<Point>(wb, nullptr));

    // Expected:
    // TYPE_OBJECT(0x22) + field_count(2) +
    //   field0: ident(0) + TYPE_INT32(0x04) + constraints(0) +
    //   field1: ident(0) + TYPE_INT32(0x04) + constraints(0)
    // = 1 + 1 + (1+1+1) + (1+1+1) = 8 bytes
    TEST_ASSERT_EQUAL(8, wb.position());

    size_t pos = 0;
    TEST_ASSERT_EQUAL(TYPE_OBJECT, buffer[pos++]);  // 0x22
    TEST_ASSERT_EQUAL(2, buffer[pos++]);            // field_count = 2

    // Field 0: x
    TEST_ASSERT_EQUAL(0, buffer[pos++]);            // empty ident (name length = 0)
    TEST_ASSERT_EQUAL(TYPE_INT32, buffer[pos++]);   // 0x04
    TEST_ASSERT_EQUAL(0, buffer[pos++]);            // no constraints

    // Field 1: y
    TEST_ASSERT_EQUAL(0, buffer[pos++]);            // empty ident
    TEST_ASSERT_EQUAL(TYPE_INT32, buffer[pos++]);   // 0x04
    TEST_ASSERT_EQUAL(0, buffer[pos++]);            // no constraints
}

void test_schema_object_with_nested_array() {
    // Test encoding a struct with nested array field
    struct Color { uint8_t r, g, b; };
    struct Pixel { int32_t x, y; Color color; };

    uint8_t buffer[64];
    WriteBuffer wb(buffer, sizeof(buffer));

    // Encode Color struct as OBJECT
    TEST_ASSERT_TRUE(SchemaTypeEncoder::encode<Color>(wb, nullptr));

    // Color has 3 fields (r, g, b), each UINT8
    // TYPE_OBJECT + 3 + (0 + UINT8 + 0) * 3 = 1 + 1 + 9 = 11 bytes
    TEST_ASSERT_EQUAL(11, wb.position());
    TEST_ASSERT_EQUAL(TYPE_OBJECT, buffer[0]);
    TEST_ASSERT_EQUAL(3, buffer[1]);  // 3 fields

    // Reset for Pixel test
    wb.reset();
    TEST_ASSERT_TRUE(SchemaTypeEncoder::encode<Pixel>(wb, nullptr));

    // Pixel has 3 fields: x (INT32), y (INT32), color (OBJECT with 3 UINT8 fields)
    // TYPE_OBJECT + 3 +
    //   field0: 0 + INT32 + 0 = 3 bytes
    //   field1: 0 + INT32 + 0 = 3 bytes
    //   field2: 0 + OBJECT + 3 + (0+UINT8+0)*3 = 1 + 11 = 12 bytes
    // Total: 1 + 1 + 3 + 3 + 12 = 20 bytes

    TEST_ASSERT_EQUAL(20, wb.position());
    TEST_ASSERT_EQUAL(TYPE_OBJECT, buffer[0]);
    TEST_ASSERT_EQUAL(3, buffer[1]);  // 3 fields (x, y, color)
}

// ============== Value<T> in Container Tests ==============

void test_value_in_array_encode_decode() {
    // Value<T> in std::array - values should encode/decode correctly
    std::array<Value<uint8_t>, 3> arr = {{{100}, {200}, {50}}};

    // Set some constraints (these don't affect encoding, but verify they're preserved)
    arr[0].constraints.flags.hasMin = true;
    arr[0].constraints.minValue[0] = 0;

    uint8_t buffer[32];
    WriteBuffer wb(buffer, sizeof(buffer));
    TEST_ASSERT_TRUE(TypeCodec::encode(wb, arr));

    // Should encode just the values: 3 bytes
    TEST_ASSERT_EQUAL(3, wb.position());
    TEST_ASSERT_EQUAL(100, buffer[0]);
    TEST_ASSERT_EQUAL(200, buffer[1]);
    TEST_ASSERT_EQUAL(50, buffer[2]);

    // Decode
    ReadBuffer rb(buffer, wb.position());
    std::array<Value<uint8_t>, 3> decoded;
    TEST_ASSERT_TRUE(TypeCodec::decode(rb, decoded));

    TEST_ASSERT_EQUAL(100, decoded[0].value);
    TEST_ASSERT_EQUAL(200, decoded[1].value);
    TEST_ASSERT_EQUAL(50, decoded[2].value);
}

void test_value_in_microlist_encode_decode() {
    // Value<T> in MicroList
    MicroList<Value<int32_t>, 4, 8> list;
    list.push_back(Value<int32_t>{-100});
    list.push_back(Value<int32_t>{0});
    list.push_back(Value<int32_t>{12345});

    uint8_t buffer[64];
    WriteBuffer wb(buffer, sizeof(buffer));
    TEST_ASSERT_TRUE(TypeCodec::encode(wb, list));

    // Should encode: varint(3) + 3 * 4 bytes = 1 + 12 = 13 bytes
    TEST_ASSERT_EQUAL(13, wb.position());

    // Decode
    ReadBuffer rb(buffer, wb.position());
    MicroList<Value<int32_t>, 4, 8> decoded;
    TEST_ASSERT_TRUE(TypeCodec::decode(rb, decoded));

    TEST_ASSERT_EQUAL(3, decoded.size());
    TEST_ASSERT_EQUAL(-100, decoded[0].value);
    TEST_ASSERT_EQUAL(0, decoded[1].value);
    TEST_ASSERT_EQUAL(12345, decoded[2].value);
}

void test_value_schema_encoding() {
    // Value<T> in schema should encode as T with constraints
    uint8_t buffer[32];
    WriteBuffer wb(buffer, sizeof(buffer));

    // Encode Value<uint8_t> type with constraints
    ValueConstraints constraints;
    constraints.flags.hasMin = true;
    constraints.flags.hasMax = true;
    constraints.minValue[0] = 0;
    constraints.maxValue[0] = 100;

    TEST_ASSERT_TRUE(SchemaTypeEncoder::encode<Value<uint8_t>>(wb, &constraints));

    // Should encode: TYPE_UINT8 + constraint_flags + min + max
    // = 1 + 1 + 1 + 1 = 4 bytes
    TEST_ASSERT_EQUAL(4, wb.position());
    TEST_ASSERT_EQUAL(TYPE_UINT8, buffer[0]);
    TEST_ASSERT_EQUAL(0x03, buffer[1]);  // has_min | has_max
    TEST_ASSERT_EQUAL(0, buffer[2]);     // min
    TEST_ASSERT_EQUAL(100, buffer[3]);   // max
}

void test_array_of_value_schema() {
    // Test schema encoding of std::array<Value<T>, N>
    uint8_t buffer[32];
    WriteBuffer wb(buffer, sizeof(buffer));

    // Encode std::array<Value<uint8_t>, 3>
    TEST_ASSERT_TRUE(encodeContainerTypeImpl(wb, static_cast<std::array<Value<uint8_t>, 3>*>(nullptr)));

    // Expected: TYPE_ARRAY + 3 + TYPE_UINT8 + 0
    TEST_ASSERT_EQUAL(4, wb.position());
    TEST_ASSERT_EQUAL(TYPE_ARRAY, buffer[0]);
    TEST_ASSERT_EQUAL(3, buffer[1]);
    TEST_ASSERT_EQUAL(TYPE_UINT8, buffer[2]);
    TEST_ASSERT_EQUAL(0, buffer[3]);  // no constraints
}

void test_list_of_value_schema() {
    // Test schema encoding of MicroList<Value<T>, I, M>
    uint8_t buffer[32];
    WriteBuffer wb(buffer, sizeof(buffer));

    // Encode MicroList<Value<int32_t>, 4, 8>
    TEST_ASSERT_TRUE(encodeContainerTypeImpl(wb, static_cast<MicroList<Value<int32_t>, 4, 8>*>(nullptr)));

    // Expected: TYPE_LIST + 0 + TYPE_INT32 + 0
    TEST_ASSERT_EQUAL(4, wb.position());
    TEST_ASSERT_EQUAL(TYPE_LIST, buffer[0]);
    TEST_ASSERT_EQUAL(0, buffer[1]);  // no container constraints
    TEST_ASSERT_EQUAL(TYPE_INT32, buffer[2]);
    TEST_ASSERT_EQUAL(0, buffer[3]);  // no element constraints
}

// ============== Oneof/Enum Validation Tests ==============

void test_oneof_validation_basic() {
    // Test oneof validation with uint8_t values
    ValueConstraints constraints;
    constraints.setOneOf<uint8_t>({1, 2, 4, 8, 16});

    TEST_ASSERT_TRUE(constraints.flags.hasOneOf);
    TEST_ASSERT_EQUAL(5, constraints.oneofCount);

    // Valid values
    TEST_ASSERT_TRUE(constraints.validate<uint8_t>(1));
    TEST_ASSERT_TRUE(constraints.validate<uint8_t>(2));
    TEST_ASSERT_TRUE(constraints.validate<uint8_t>(4));
    TEST_ASSERT_TRUE(constraints.validate<uint8_t>(8));
    TEST_ASSERT_TRUE(constraints.validate<uint8_t>(16));

    // Invalid values
    TEST_ASSERT_FALSE(constraints.validate<uint8_t>(0));
    TEST_ASSERT_FALSE(constraints.validate<uint8_t>(3));
    TEST_ASSERT_FALSE(constraints.validate<uint8_t>(5));
    TEST_ASSERT_FALSE(constraints.validate<uint8_t>(255));
}

void test_oneof_validation_int32() {
    // Test oneof with int32_t values
    ValueConstraints constraints;
    constraints.setOneOf<int32_t>({-100, 0, 100, 1000});

    TEST_ASSERT_TRUE(constraints.validate<int32_t>(-100));
    TEST_ASSERT_TRUE(constraints.validate<int32_t>(0));
    TEST_ASSERT_TRUE(constraints.validate<int32_t>(100));
    TEST_ASSERT_TRUE(constraints.validate<int32_t>(1000));

    TEST_ASSERT_FALSE(constraints.validate<int32_t>(-101));
    TEST_ASSERT_FALSE(constraints.validate<int32_t>(1));
    TEST_ASSERT_FALSE(constraints.validate<int32_t>(999));
}

void test_oneof_with_min_max() {
    // Test combining oneof with min/max (all constraints must pass)
    ValueConstraints constraints;
    constraints.setMin<uint8_t>(0);
    constraints.setMax<uint8_t>(255);
    constraints.setOneOf<uint8_t>({0, 50, 100, 150, 200, 255});

    // Must be in oneof set AND within min/max
    TEST_ASSERT_TRUE(constraints.validate<uint8_t>(0));
    TEST_ASSERT_TRUE(constraints.validate<uint8_t>(100));
    TEST_ASSERT_TRUE(constraints.validate<uint8_t>(255));

    // Not in oneof set
    TEST_ASSERT_FALSE(constraints.validate<uint8_t>(25));
    TEST_ASSERT_FALSE(constraints.validate<uint8_t>(75));
}

void test_oneof_get_values() {
    // Test retrieving oneof values by index
    ValueConstraints constraints;
    constraints.setOneOf<uint8_t>({10, 20, 30});

    TEST_ASSERT_EQUAL(10, constraints.getOneOf<uint8_t>(0));
    TEST_ASSERT_EQUAL(20, constraints.getOneOf<uint8_t>(1));
    TEST_ASSERT_EQUAL(30, constraints.getOneOf<uint8_t>(2));
    TEST_ASSERT_EQUAL(0, constraints.getOneOf<uint8_t>(99));  // Out of bounds
}

void test_oneof_add_single_values() {
    // Test adding oneof values one at a time
    ValueConstraints constraints;
    TEST_ASSERT_TRUE(constraints.addOneOf<uint8_t>(5));
    TEST_ASSERT_TRUE(constraints.addOneOf<uint8_t>(10));
    TEST_ASSERT_TRUE(constraints.addOneOf<uint8_t>(15));

    TEST_ASSERT_TRUE(constraints.flags.hasOneOf);
    TEST_ASSERT_EQUAL(3, constraints.oneofCount);

    TEST_ASSERT_TRUE(constraints.validate<uint8_t>(5));
    TEST_ASSERT_TRUE(constraints.validate<uint8_t>(10));
    TEST_ASSERT_TRUE(constraints.validate<uint8_t>(15));
    TEST_ASSERT_FALSE(constraints.validate<uint8_t>(7));
}

void test_oneof_schema_encoding() {
    // Test schema encoding of oneof constraints
    uint8_t buffer[64];
    WriteBuffer wb(buffer, sizeof(buffer));

    ValueConstraints constraints;
    constraints.setOneOf<uint8_t>({1, 2, 4});

    TEST_ASSERT_TRUE(SchemaTypeEncoder::encode<uint8_t>(wb, &constraints));

    // Expected: TYPE_UINT8 + flags(hasOneOf=0x08) + varint(count=3) + values
    // = 1 + 1 + 1 + 3 = 6 bytes
    TEST_ASSERT_EQUAL(6, wb.position());
    TEST_ASSERT_EQUAL(TYPE_UINT8, buffer[0]);
    TEST_ASSERT_EQUAL(0x08, buffer[1]);  // hasOneOf flag
    TEST_ASSERT_EQUAL(3, buffer[2]);     // oneof count
    TEST_ASSERT_EQUAL(1, buffer[3]);     // value 1
    TEST_ASSERT_EQUAL(2, buffer[4]);     // value 2
    TEST_ASSERT_EQUAL(4, buffer[5]);     // value 3
}

void test_oneof_schema_with_int32() {
    // Test schema encoding of oneof with int32 values
    uint8_t buffer[64];
    WriteBuffer wb(buffer, sizeof(buffer));

    ValueConstraints constraints;
    constraints.setOneOf<int32_t>({100, 200});

    TEST_ASSERT_TRUE(SchemaTypeEncoder::encode<int32_t>(wb, &constraints));

    // Expected: TYPE_INT32 + flags(0x08) + varint(2) + 2 * 4 bytes
    // = 1 + 1 + 1 + 8 = 11 bytes
    TEST_ASSERT_EQUAL(11, wb.position());
    TEST_ASSERT_EQUAL(TYPE_INT32, buffer[0]);
    TEST_ASSERT_EQUAL(0x08, buffer[1]);  // hasOneOf flag
    TEST_ASSERT_EQUAL(2, buffer[2]);     // oneof count

    // First int32 value: 100 (little-endian)
    int32_t val1;
    memcpy(&val1, &buffer[3], sizeof(int32_t));
    TEST_ASSERT_EQUAL(100, val1);

    // Second int32 value: 200
    int32_t val2;
    memcpy(&val2, &buffer[7], sizeof(int32_t));
    TEST_ASSERT_EQUAL(200, val2);
}

void test_constraints_builder_oneof() {
    // Test the Constraints<T> builder with oneof
    auto c = Constraints<uint8_t>().oneof({1, 2, 3, 4, 5});

    TEST_ASSERT_TRUE(c.value.flags.hasOneOf);
    TEST_ASSERT_EQUAL(5, c.value.oneofCount);

    TEST_ASSERT_TRUE(c.value.validate<uint8_t>(3));
    TEST_ASSERT_FALSE(c.value.validate<uint8_t>(6));
}

// ============== Runtime Validation Enforcement Tests ==============

void test_property_runtime_oneof_validation() {
    // Test that Property::operator= rejects values not in oneof set
    Property<uint8_t> mode("mode", 1, PropertyLevel::LOCAL,
        Constraints<uint8_t>().oneof({1, 2, 4, 8}));

    TEST_ASSERT_EQUAL(1, mode.get());

    // Valid values should be accepted
    mode = 2;
    TEST_ASSERT_EQUAL(2, mode.get());

    mode = 4;
    TEST_ASSERT_EQUAL(4, mode.get());

    // Invalid value should be rejected (stays at previous value)
    mode = 3;  // Not in {1, 2, 4, 8}
    TEST_ASSERT_EQUAL(4, mode.get());  // Should still be 4

    mode = 5;  // Not in set
    TEST_ASSERT_EQUAL(4, mode.get());  // Should still be 4

    // Another valid value
    mode = 8;
    TEST_ASSERT_EQUAL(8, mode.get());
}

void test_property_runtime_minmax_validation() {
    // Test that Property::operator= rejects values outside min/max
    Property<int32_t> temp("temp", 50, PropertyLevel::LOCAL,
        Constraints<int32_t>().min(0).max(100));

    TEST_ASSERT_EQUAL(50, temp.get());

    // Valid value
    temp = 75;
    TEST_ASSERT_EQUAL(75, temp.get());

    // Out of range (too high)
    temp = 150;
    TEST_ASSERT_EQUAL(75, temp.get());  // Rejected

    // Out of range (too low)
    temp = -10;
    TEST_ASSERT_EQUAL(75, temp.get());  // Rejected

    // Edge values
    temp = 0;
    TEST_ASSERT_EQUAL(0, temp.get());  // Accepted (min is valid)

    temp = 100;
    TEST_ASSERT_EQUAL(100, temp.get());  // Accepted (max is valid)
}

void test_property_tryset_returns_success() {
    // Test that trySet() returns proper success/failure
    Property<uint8_t> mode("mode", 1, PropertyLevel::LOCAL,
        Constraints<uint8_t>().oneof({1, 2, 4}));

    TEST_ASSERT_TRUE(mode.trySet(2));   // Valid
    TEST_ASSERT_EQUAL(2, mode.get());

    TEST_ASSERT_FALSE(mode.trySet(3));  // Invalid
    TEST_ASSERT_EQUAL(2, mode.get());   // Unchanged

    TEST_ASSERT_TRUE(mode.trySet(4));   // Valid
    TEST_ASSERT_EQUAL(4, mode.get());
}

void test_array_property_runtime_element_validation() {
    // Test that ArrayProperty::set() validates element constraints
    ArrayProperty<uint8_t, 3> rgb("rgb", {128, 128, 128}, PropertyLevel::LOCAL,
        ArrayConstraints<uint8_t>().min(0).max(200));

    TEST_ASSERT_EQUAL(128, rgb[0]);

    // Valid value
    TEST_ASSERT_TRUE(rgb.set(0, 100));
    TEST_ASSERT_EQUAL(100, rgb[0]);

    // Invalid value (exceeds max)
    TEST_ASSERT_FALSE(rgb.set(0, 250));
    TEST_ASSERT_EQUAL(100, rgb[0]);  // Unchanged
}

void test_list_property_runtime_element_validation() {
    // Test that list push_back and set validate element constraints
    Property<MicroList<uint8_t, 4, 8>> levels("levels", {50}, PropertyLevel::LOCAL,
        ListConstraints<uint8_t>().elementMin(0).elementMax(100));

    TEST_ASSERT_EQUAL(1, levels.size());
    TEST_ASSERT_EQUAL(50, levels[0]);

    // Valid push_back
    TEST_ASSERT_TRUE(levels.push_back(75));
    TEST_ASSERT_EQUAL(2, levels.size());

    // Invalid push_back (exceeds max)
    TEST_ASSERT_FALSE(levels.push_back(150));
    TEST_ASSERT_EQUAL(2, levels.size());  // Not added

    // Valid set
    TEST_ASSERT_TRUE(levels.set(0, 25));
    TEST_ASSERT_EQUAL(25, levels[0]);

    // Invalid set
    TEST_ASSERT_FALSE(levels.set(0, 200));
    TEST_ASSERT_EQUAL(25, levels[0]);  // Unchanged
}

// ============== Main ==============

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // ArrayProperty tests
    RUN_TEST(test_array_property_basic);
    RUN_TEST(test_array_property_type_info);
    RUN_TEST(test_array_property_set_element);
    RUN_TEST(test_array_property_set_all);
    RUN_TEST(test_array_property_int32);
    RUN_TEST(test_array_property_iterator);

    // ListProperty tests
    RUN_TEST(test_list_property_empty);
    RUN_TEST(test_list_property_type_info);
    RUN_TEST(test_list_property_push_pop);
    RUN_TEST(test_list_property_initializer);
    RUN_TEST(test_list_property_string);
    RUN_TEST(test_list_property_set_string);
    RUN_TEST(test_list_property_clear);
    RUN_TEST(test_list_property_resize);
    RUN_TEST(test_list_property_iterator);

    // Wire format tests
    RUN_TEST(test_array_encode);
    RUN_TEST(test_array_encode_int32);
    RUN_TEST(test_list_encode);
    RUN_TEST(test_list_encode_empty);
    RUN_TEST(test_list_encode_string);
    RUN_TEST(test_array_decode);
    RUN_TEST(test_list_decode);
    RUN_TEST(test_list_decode_truncate);

    // Property<MicroList<...>> tests
    RUN_TEST(test_property_microlist_empty);
    RUN_TEST(test_property_microlist_initializer);
    RUN_TEST(test_property_microlist_push_pop);
    RUN_TEST(test_property_microlist_type_info);
    RUN_TEST(test_property_microlist_clear);
    RUN_TEST(test_property_microlist_resize);
    RUN_TEST(test_property_microlist_set_element);
    RUN_TEST(test_property_microlist_iterator);
    RUN_TEST(test_property_microlist_encode);
    RUN_TEST(test_property_microlist_decode);
    RUN_TEST(test_property_microlist_encode_int32);
    RUN_TEST(test_property_microlist_decode_int32);
    RUN_TEST(test_microlist_typecodec_encode);
    RUN_TEST(test_microlist_typecodec_decode);

    // Nested container tests
    RUN_TEST(test_nested_type_traits);
    RUN_TEST(test_nested_list_of_arrays_encode);
    RUN_TEST(test_nested_list_of_arrays_decode);
    RUN_TEST(test_nested_array_of_lists_encode);
    RUN_TEST(test_nested_array_of_lists_decode);
    RUN_TEST(test_nested_list_of_lists_encode);
    RUN_TEST(test_nested_list_of_lists_decode);
    RUN_TEST(test_deeply_nested_encode_decode);
    RUN_TEST(test_struct_encode_decode);
    RUN_TEST(test_array_of_structs_encode_decode);

    // std::string in containers
    RUN_TEST(test_string_type_traits);
    RUN_TEST(test_string_encode_decode);
    RUN_TEST(test_string_empty_encode_decode);
    RUN_TEST(test_microlist_of_strings_encode_decode);
    RUN_TEST(test_array_of_strings_encode_decode);
    RUN_TEST(test_nested_string_containers);

    // Schema type encoding
    RUN_TEST(test_schema_basic_type);
    RUN_TEST(test_schema_basic_type_with_constraints);
    RUN_TEST(test_schema_array_of_basic);
    RUN_TEST(test_schema_list_of_basic);
    RUN_TEST(test_schema_nested_array_of_list);
    RUN_TEST(test_schema_nested_list_of_array);
    RUN_TEST(test_schema_string_as_list);
    RUN_TEST(test_schema_deeply_nested);
    RUN_TEST(test_schema_object_with_fields);
    RUN_TEST(test_schema_object_with_nested_array);

    // Value<T> in container tests
    RUN_TEST(test_value_in_array_encode_decode);
    RUN_TEST(test_value_in_microlist_encode_decode);
    RUN_TEST(test_value_schema_encoding);
    RUN_TEST(test_array_of_value_schema);
    RUN_TEST(test_list_of_value_schema);

    // Oneof/enum validation tests
    RUN_TEST(test_oneof_validation_basic);
    RUN_TEST(test_oneof_validation_int32);
    RUN_TEST(test_oneof_with_min_max);
    RUN_TEST(test_oneof_get_values);
    RUN_TEST(test_oneof_add_single_values);
    RUN_TEST(test_oneof_schema_encoding);
    RUN_TEST(test_oneof_schema_with_int32);
    RUN_TEST(test_constraints_builder_oneof);

    // Runtime validation enforcement tests
    RUN_TEST(test_property_runtime_oneof_validation);
    RUN_TEST(test_property_runtime_minmax_validation);
    RUN_TEST(test_property_tryset_returns_success);
    RUN_TEST(test_array_property_runtime_element_validation);
    RUN_TEST(test_list_property_runtime_element_validation);

    return UNITY_END();
}
