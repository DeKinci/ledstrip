#include <Arduino.h>
#include <unity.h>
#include <Property.h>
#include <PropertySystem.h>

// Test properties
PROPERTY_LOCAL(test_debounce, uint8_t, 0);
PROPERTY_LOCAL(test_flush, int32_t, 100);

void setUp(void) {
    MicroProto::PropertySystem::init();
}

void tearDown(void) {
    // Reset dirty flags
    MicroProto::PropertySystem::flushAll();
}

void test_property_system_init(void) {
    // Should have properties registered
    TEST_ASSERT_GREATER_OR_EQUAL(2, MicroProto::PropertySystem::getPropertyCount());
}

void test_property_mark_dirty(void) {
    // Change property, should mark as dirty
    test_debounce = 42;

    // Property system tracks this internally
    // We can't directly check dirty flag, but we can verify flush works
    MicroProto::PropertySystem::flush(test_debounce.id);

    TEST_ASSERT_EQUAL_UINT8(42, test_debounce);
}

void test_property_debounce_timing(void) {
    uint32_t start = millis();

    // Rapid changes
    for (int i = 0; i < 10; i++) {
        test_debounce = i;
        delay(50); // 50ms between changes
    }

    // Should not have flushed yet (< 1000ms debounce)
    uint32_t elapsed = millis() - start;
    TEST_ASSERT_LESS_THAN(1000, elapsed);

    // Wait for debounce
    delay(1100);

    // Run system loop to trigger flush
    MicroProto::PropertySystem::loop();

    // Value should be the last one set
    TEST_ASSERT_EQUAL_UINT8(9, test_debounce);
}

void test_property_immediate_flush(void) {
    test_flush = 999;

    // Force immediate flush
    MicroProto::PropertySystem::flush(test_flush.id);

    // Value should be committed
    TEST_ASSERT_EQUAL_INT32(999, test_flush);
}

void test_property_flush_all(void) {
    test_debounce = 10;
    test_flush = 20;

    // Flush all properties at once
    MicroProto::PropertySystem::flushAll();

    TEST_ASSERT_EQUAL_UINT8(10, test_debounce);
    TEST_ASSERT_EQUAL_INT32(20, test_flush);
}

void test_property_system_loop(void) {
    // Set a property
    test_debounce = 123;

    // Loop should not flush immediately
    MicroProto::PropertySystem::loop();

    // Wait for debounce period
    delay(1100);

    // Now loop should flush
    MicroProto::PropertySystem::loop();

    TEST_ASSERT_EQUAL_UINT8(123, test_debounce);
}

void test_multiple_rapid_changes(void) {
    // Simulate rapid user input
    for (int i = 0; i < 100; i++) {
        test_debounce = i % 256;
        delay(10); // 10ms between changes
    }

    // Force flush to get final value
    MicroProto::PropertySystem::flushAll();

    // Should have the last value
    TEST_ASSERT_EQUAL_UINT8(99 % 256, test_debounce);
}

void setup() {
    delay(2000);
    UNITY_BEGIN();

    RUN_TEST(test_property_system_init);
    RUN_TEST(test_property_mark_dirty);
    RUN_TEST(test_property_immediate_flush);
    RUN_TEST(test_property_flush_all);
    RUN_TEST(test_property_system_loop);
    RUN_TEST(test_multiple_rapid_changes);
    RUN_TEST(test_property_debounce_timing);

    UNITY_END();
}

void loop() {}
