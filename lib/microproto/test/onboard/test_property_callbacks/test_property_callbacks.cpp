#include <Arduino.h>
#include <unity.h>
#include <Property.h>
#include <PropertySystem.h>

using namespace MicroProto;

// Test properties
Property<uint8_t> cb_test("cb_test", 100, PropertyLevel::LOCAL);
Property<int32_t> cb_chain("cb_chain", 0, PropertyLevel::LOCAL);

// Callback tracking
static volatile int callback_count = 0;

void setUp(void) {
    cb_test = 100;
    cb_chain = 0;
    callback_count = 0;
    cb_test.clearOnChange();
    cb_chain.clearOnChange();
}

void tearDown(void) {
    cb_test.clearOnChange();
    cb_chain.clearOnChange();
}

void test_callback_fires_on_change(void) {
    cb_test.onChange([]() {
        callback_count++;
    });

    cb_test = 150;
    TEST_ASSERT_EQUAL(1, callback_count);
}

void test_callback_multiple_changes(void) {
    cb_test.onChange([]() {
        callback_count++;
    });

    cb_test = 10;
    cb_test = 20;
    cb_test = 30;

    TEST_ASSERT_EQUAL(3, callback_count);
}

void test_callback_no_fire_on_same_value(void) {
    cb_test = 50;

    cb_test.onChange([]() {
        callback_count++;
    });

    // Set to same value — Property::operator= skips if no change
    cb_test = 50;
    TEST_ASSERT_EQUAL(0, callback_count);
}

void test_callback_removal(void) {
    cb_test.onChange([]() {
        callback_count++;
    });

    cb_test = 60;
    TEST_ASSERT_EQUAL(1, callback_count);

    cb_test.clearOnChange();

    cb_test = 70;
    TEST_ASSERT_EQUAL(1, callback_count);  // not called again
}

void test_callback_chaining(void) {
    // Changing cb_test sets cb_chain in the callback
    cb_test.onChange([]() {
        cb_chain = (int32_t)cb_test.get() * 2;
    });

    cb_test = 25;
    TEST_ASSERT_EQUAL_INT32(50, cb_chain.get());
}

void test_typed_callback(void) {
    static uint8_t last_old = 0;
    static uint8_t last_new = 0;

    cb_test.onChangeTyped([](uint8_t old_val, uint8_t new_val) {
        last_old = old_val;
        last_new = new_val;
    });

    cb_test = 200;

    TEST_ASSERT_EQUAL_UINT8(100, last_old);
    TEST_ASSERT_EQUAL_UINT8(200, last_new);
}

void test_typed_callback_tracks_transitions(void) {
    static uint8_t last_old = 0;
    static uint8_t last_new = 0;

    cb_test.onChangeTyped([](uint8_t old_val, uint8_t new_val) {
        last_old = old_val;
        last_new = new_val;
    });

    cb_test = 10;
    TEST_ASSERT_EQUAL_UINT8(100, last_old);
    TEST_ASSERT_EQUAL_UINT8(10, last_new);

    cb_test = 20;
    TEST_ASSERT_EQUAL_UINT8(10, last_old);
    TEST_ASSERT_EQUAL_UINT8(20, last_new);
}

void setup() {
    delay(2000);
    UNITY_BEGIN();

    RUN_TEST(test_callback_fires_on_change);
    RUN_TEST(test_callback_multiple_changes);
    RUN_TEST(test_callback_no_fire_on_same_value);
    RUN_TEST(test_callback_removal);
    RUN_TEST(test_callback_chaining);
    RUN_TEST(test_typed_callback);
    RUN_TEST(test_typed_callback_tracks_transitions);

    UNITY_END();
}

void loop() {}
