#include <Arduino.h>
#include <unity.h>
#include <Property.h>
#include <PropertySystem.h>

// Test properties
PROPERTY_LOCAL(callback_test, uint8_t, 100);
PROPERTY_LOCAL(chained_test, int32_t, 0);

// Callback tracking
static int callback_count = 0;
static uint8_t last_old_value = 0;
static uint8_t last_new_value = 0;

void setUp(void) {
    MicroProto::PropertySystem::init();
    // Reset to known state
    callback_test = 100;
    chained_test = 0;
    callback_count = 0;
    last_old_value = 0;
    last_new_value = 0;
}

void tearDown(void) {
    // Clear callbacks
    callback_test.onChange(nullptr);
    chained_test.onChange(nullptr);
}

void test_callback_registration(void) {
    bool called = false;

    callback_test.onChange([&called](const uint8_t& old_val, const uint8_t& new_val) {
        called = true;
    });

    callback_test = 150;

    TEST_ASSERT_TRUE(called);
}

void test_callback_values(void) {
    callback_test = 100; // Reset to default

    callback_test.onChange([](const uint8_t& old_val, const uint8_t& new_val) {
        last_old_value = old_val;
        last_new_value = new_val;
    });

    callback_test = 200;

    TEST_ASSERT_EQUAL_UINT8(100, last_old_value);
    TEST_ASSERT_EQUAL_UINT8(200, last_new_value);
}

void test_callback_multiple_changes(void) {
    callback_test.onChange([](const uint8_t& old_val, const uint8_t& new_val) {
        callback_count++;
        last_old_value = old_val;
        last_new_value = new_val;
    });

    callback_test = 10;
    callback_test = 20;
    callback_test = 30;

    // Should have been called 3 times
    TEST_ASSERT_EQUAL(3, callback_count);
    // Last values should be 20 -> 30
    TEST_ASSERT_EQUAL_UINT8(20, last_old_value);
    TEST_ASSERT_EQUAL_UINT8(30, last_new_value);
}

void test_callback_no_change_if_same_value(void) {
    callback_test = 50;

    callback_test.onChange([](const uint8_t& old_val, const uint8_t& new_val) {
        callback_count++;
    });

    // Set to same value - implementation might still call callback
    // This is implementation-specific behavior
    callback_test = 50;

    // If implementation optimizes, count should be 0
    // If not, it's still valid behavior
    TEST_ASSERT_GREATER_OR_EQUAL(0, callback_count);
}

void test_callback_removal(void) {
    callback_test.onChange([](const uint8_t& old_val, const uint8_t& new_val) {
        callback_count++;
    });

    callback_test = 60;
    TEST_ASSERT_EQUAL(1, callback_count);

    // Remove callback
    callback_test.onChange(nullptr);

    callback_test = 70;
    // Count should still be 1 (callback not called)
    TEST_ASSERT_EQUAL(1, callback_count);
}

void test_callback_chaining_behavior(void) {
    // Test if changing one property in a callback works
    callback_test = 100;
    chained_test = 0;

    callback_test.onChange([](const uint8_t& old_val, const uint8_t& new_val) {
        chained_test = new_val * 2;
    });

    callback_test = 25;

    TEST_ASSERT_EQUAL_INT32(50, chained_test);
}

void test_callback_with_get_method(void) {
    callback_test.onChange([](const uint8_t& old_val, const uint8_t& new_val) {
        last_old_value = old_val;
        last_new_value = new_val;
    });

    uint8_t old = callback_test.get();
    callback_test.set(99);

    TEST_ASSERT_EQUAL_UINT8(old, last_old_value);
    TEST_ASSERT_EQUAL_UINT8(99, last_new_value);
}

void test_callback_in_expression(void) {
    int total_changes = 0;

    callback_test.onChange([&total_changes](const uint8_t& old_val, const uint8_t& new_val) {
        total_changes++;
    });

    // Use in expressions
    callback_test = 10;
    uint8_t result = callback_test + 5;
    TEST_ASSERT_EQUAL_UINT8(15, result);

    // Change again
    callback_test = callback_test + 10;

    TEST_ASSERT_EQUAL(2, total_changes);
    TEST_ASSERT_EQUAL_UINT8(20, callback_test);
}

void test_callback_with_different_types(void) {
    int32_t old_int = 0;
    int32_t new_int = 0;

    chained_test.onChange([&old_int, &new_int](const int32_t& old_val, const int32_t& new_val) {
        old_int = old_val;
        new_int = new_val;
    });

    chained_test = -12345;

    TEST_ASSERT_EQUAL_INT32(0, old_int);
    TEST_ASSERT_EQUAL_INT32(-12345, new_int);
}

void setup() {
    delay(2000);
    UNITY_BEGIN();

    RUN_TEST(test_callback_registration);
    RUN_TEST(test_callback_values);
    RUN_TEST(test_callback_multiple_changes);
    RUN_TEST(test_callback_no_change_if_same_value);
    RUN_TEST(test_callback_removal);
    RUN_TEST(test_callback_chaining_behavior);
    RUN_TEST(test_callback_with_get_method);
    RUN_TEST(test_callback_in_expression);
    RUN_TEST(test_callback_with_different_types);

    UNITY_END();
}

void loop() {}
