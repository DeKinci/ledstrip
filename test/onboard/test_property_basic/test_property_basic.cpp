#include <Arduino.h>
#include <unity.h>
#include <Property.h>
#include <PropertySystem.h>

// Define test properties
PROPERTY_LOCAL(test_uint8, uint8_t, 100);
PROPERTY_LOCAL(test_int32, int32_t, -50);
PROPERTY_LOCAL(test_bool, bool, true);
PROPERTY_LOCAL(test_float, float, 3.14f);

void setUp(void) {
    MicroProto::PropertySystem::init();
}

void tearDown(void) {
    // Called after each test
}

void test_property_auto_registration(void) {
    // Properties should auto-register (this test file defines 4)
    uint8_t count = MicroProto::PropertySystem::getPropertyCount();
    TEST_ASSERT_EQUAL_UINT8(4, count);
}

void test_property_read_default(void) {
    // Should read default values
    TEST_ASSERT_EQUAL_UINT8(100, test_uint8);
    TEST_ASSERT_EQUAL_INT32(-50, test_int32);
    TEST_ASSERT_TRUE(test_bool);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.14f, test_float);
}

void test_property_write_read(void) {
    // Write and read back
    test_uint8 = 200;
    TEST_ASSERT_EQUAL_UINT8(200, test_uint8);

    test_int32 = 12345;
    TEST_ASSERT_EQUAL_INT32(12345, test_int32);

    test_bool = false;
    TEST_ASSERT_FALSE(test_bool);

    test_float = 2.71f;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.71f, test_float);
}

void test_property_implicit_cast(void) {
    test_uint8 = 150;

    // Should work with implicit cast
    uint8_t value = test_uint8;
    TEST_ASSERT_EQUAL_UINT8(150, value);

    // Should work in expressions
    uint8_t result = test_uint8 + 10;
    TEST_ASSERT_EQUAL_UINT8(160, result);

    // Should work in comparisons
    TEST_ASSERT_TRUE(test_uint8 > 100);
    TEST_ASSERT_TRUE(test_uint8 < 200);
}

void test_property_assignment_operator(void) {
    // Assignment should work
    test_uint8 = 50;
    TEST_ASSERT_EQUAL_UINT8(50, test_uint8);

    // Chained assignment
    test_int32 = 100;
    TEST_ASSERT_EQUAL_INT32(100, test_int32);
}

void test_property_get_set_methods(void) {
    // get() method
    test_uint8 = 75;
    TEST_ASSERT_EQUAL_UINT8(75, test_uint8.get());

    // set() method
    test_uint8.set(125);
    TEST_ASSERT_EQUAL_UINT8(125, test_uint8);
}

void test_property_metadata(void) {
    // Find property in linked list
    MicroProto::PropertyBase* prop = nullptr;
    for (MicroProto::PropertyBase* p = MicroProto::PropertyBase::head; p; p = p->next) {
        if (strcmp(p->name, "test_uint8") == 0) {
            prop = p;
            break;
        }
    }

    TEST_ASSERT_NOT_NULL(prop);
    TEST_ASSERT_EQUAL_STRING("test_uint8", prop->name);
    TEST_ASSERT_EQUAL(MicroProto::PropertyLevel::LOCAL, prop->level);
    TEST_ASSERT_FALSE(prop->persistent);
    TEST_ASSERT_FALSE(prop->readonly);
}

void test_property_type_id(void) {
    // Find properties and check type IDs
    for (MicroProto::PropertyBase* p = MicroProto::PropertyBase::head; p; p = p->next) {
        if (strcmp(p->name, "test_uint8") == 0) {
            TEST_ASSERT_EQUAL_UINT8(MicroProto::TYPE_UINT8, p->getTypeId());
        }
        if (strcmp(p->name, "test_int32") == 0) {
            TEST_ASSERT_EQUAL_UINT8(MicroProto::TYPE_INT32, p->getTypeId());
        }
        if (strcmp(p->name, "test_bool") == 0) {
            TEST_ASSERT_EQUAL_UINT8(MicroProto::TYPE_BOOL, p->getTypeId());
        }
        if (strcmp(p->name, "test_float") == 0) {
            TEST_ASSERT_EQUAL_UINT8(MicroProto::TYPE_FLOAT32, p->getTypeId());
        }
    }
}

void setup() {
    delay(2000); // Wait for serial
    UNITY_BEGIN();

    RUN_TEST(test_property_auto_registration);
    RUN_TEST(test_property_read_default);
    RUN_TEST(test_property_write_read);
    RUN_TEST(test_property_implicit_cast);
    RUN_TEST(test_property_assignment_operator);
    RUN_TEST(test_property_get_set_methods);
    RUN_TEST(test_property_metadata);
    RUN_TEST(test_property_type_id);

    UNITY_END();
}

void loop() {}
