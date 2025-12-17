#include <unity.h>
#include <array>
#include "ArrayProperty.h"
#include "ListProperty.h"
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

    return UNITY_END();
}
