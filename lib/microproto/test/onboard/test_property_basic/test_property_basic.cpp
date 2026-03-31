#include <Arduino.h>
#include <unity.h>
#include <Property.h>
#include <PropertySystem.h>

using namespace MicroProto;
using PB = PropertyBase;

// Test properties (current API — global instances, auto-register)
Property<uint8_t> test_uint8("test_uint8", 100, PropertyLevel::LOCAL);
Property<int32_t> test_int32("test_int32", -50, PropertyLevel::LOCAL);
Property<bool>    test_bool("test_bool", true, PropertyLevel::LOCAL);
Property<float>   test_float("test_float", 3.14f, PropertyLevel::LOCAL);

void setUp(void) {}
void tearDown(void) {}

void test_property_auto_registration(void) {
    TEST_ASSERT_GREATER_OR_EQUAL(4, PropertyBase::count);
}

void test_property_read_default(void) {
    TEST_ASSERT_EQUAL_UINT8(100, test_uint8.get());
    TEST_ASSERT_EQUAL_INT32(-50, test_int32.get());
    TEST_ASSERT_TRUE(test_bool.get());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.14f, test_float.get());
}

void test_property_write_read(void) {
    test_uint8 = 200;
    TEST_ASSERT_EQUAL_UINT8(200, test_uint8.get());

    test_int32 = 12345;
    TEST_ASSERT_EQUAL_INT32(12345, test_int32.get());

    test_bool = false;
    TEST_ASSERT_FALSE(test_bool.get());

    test_float = 2.71f;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.71f, test_float.get());

    // Restore defaults for other tests
    test_uint8 = 100;
    test_int32 = -50;
    test_bool = true;
    test_float = 3.14f;
}

void test_property_implicit_cast(void) {
    test_uint8 = 150;

    uint8_t value = test_uint8;
    TEST_ASSERT_EQUAL_UINT8(150, value);

    uint8_t result = (uint8_t)test_uint8 + 10;
    TEST_ASSERT_EQUAL_UINT8(160, result);

    TEST_ASSERT_TRUE(test_uint8.get() > 100);
    TEST_ASSERT_TRUE(test_uint8.get() < 200);

    test_uint8 = 100;
}

void test_property_assignment_operator(void) {
    test_uint8 = 50;
    TEST_ASSERT_EQUAL_UINT8(50, test_uint8.get());

    test_int32 = 100;
    TEST_ASSERT_EQUAL_INT32(100, test_int32.get());

    test_uint8 = 100;
    test_int32 = -50;
}

void test_property_get_set_methods(void) {
    test_uint8 = 75;
    TEST_ASSERT_EQUAL_UINT8(75, test_uint8.get());

    test_uint8.set(125);
    TEST_ASSERT_EQUAL_UINT8(125, test_uint8.get());

    test_uint8 = 100;
}

void test_property_metadata(void) {
    PropertyBase* prop = PropertyBase::find(test_uint8.id);

    TEST_ASSERT_NOT_NULL(prop);
    TEST_ASSERT_EQUAL_STRING("test_uint8", prop->name);
    TEST_ASSERT_EQUAL(PropertyLevel::LOCAL, prop->level);
    TEST_ASSERT_FALSE(prop->persistent);
    TEST_ASSERT_FALSE(prop->readonly);
    TEST_ASSERT_FALSE(prop->isStream());
}

void test_property_type_id(void) {
    TEST_ASSERT_EQUAL_UINT8(TYPE_UINT8, PropertyBase::find(test_uint8.id)->getTypeId());
    TEST_ASSERT_EQUAL_UINT8(TYPE_INT32, PropertyBase::find(test_int32.id)->getTypeId());
    TEST_ASSERT_EQUAL_UINT8(TYPE_BOOL, PropertyBase::find(test_bool.id)->getTypeId());
    TEST_ASSERT_EQUAL_UINT8(TYPE_FLOAT32, PropertyBase::find(test_float.id)->getTypeId());
}

void test_property_no_change_skip(void) {
    test_uint8 = 42;
    // Setting to same value should be a no-op (no dirty mark)
    test_uint8 = 42;
    TEST_ASSERT_EQUAL_UINT8(42, test_uint8.get());

    test_uint8 = 100;
}

void setup() {
    delay(2000);
    UNITY_BEGIN();

    RUN_TEST(test_property_auto_registration);
    RUN_TEST(test_property_read_default);
    RUN_TEST(test_property_write_read);
    RUN_TEST(test_property_implicit_cast);
    RUN_TEST(test_property_assignment_operator);
    RUN_TEST(test_property_get_set_methods);
    RUN_TEST(test_property_metadata);
    RUN_TEST(test_property_type_id);
    RUN_TEST(test_property_no_change_skip);

    UNITY_END();
}

void loop() {}
