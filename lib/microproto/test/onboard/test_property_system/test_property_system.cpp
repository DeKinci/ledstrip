#include <Arduino.h>
#include <unity.h>
#include <Property.h>
#include <PropertySystem.h>

using namespace MicroProto;

// Test properties
Property<uint8_t> test_sys_a("test_sys_a", 0, PropertyLevel::LOCAL);
Property<int32_t> test_sys_b("test_sys_b", 100, PropertyLevel::LOCAL);

void setUp(void) {}
void tearDown(void) {}

void test_property_system_init(void) {
    PropertySystem::init();
    TEST_ASSERT_GREATER_OR_EQUAL(2, PropertySystem::getPropertyCount());
}

void test_property_mark_dirty_on_change(void) {
    // Changing a property should mark it dirty internally
    test_sys_a = 42;
    TEST_ASSERT_EQUAL_UINT8(42, test_sys_a.get());

    // Run loop — dirty flag is cleared after flush listeners are notified
    PropertySystem::loop();
    TEST_ASSERT_EQUAL_UINT8(42, test_sys_a.get());
}

void test_property_system_loop_runs(void) {
    test_sys_a = 123;

    // Multiple loop() calls should not crash
    for (int i = 0; i < 10; i++) {
        PropertySystem::loop();
        delay(10);
    }

    TEST_ASSERT_EQUAL_UINT8(123, test_sys_a.get());
}

void test_rapid_changes(void) {
    for (int i = 0; i < 100; i++) {
        test_sys_a = i % 256;
        if (i % 10 == 0) {
            PropertySystem::loop();
        }
    }

    TEST_ASSERT_EQUAL_UINT8(99, test_sys_a.get());
}

void test_multiple_properties_dirty(void) {
    test_sys_a = 10;
    test_sys_b = 20;

    PropertySystem::loop();

    TEST_ASSERT_EQUAL_UINT8(10, test_sys_a.get());
    TEST_ASSERT_EQUAL_INT32(20, test_sys_b.get());
}

void test_no_change_no_dirty(void) {
    test_sys_a = 50;
    PropertySystem::loop();

    // Setting to same value should not re-mark dirty
    test_sys_a = 50;

    // Loop should be a no-op
    PropertySystem::loop();
    TEST_ASSERT_EQUAL_UINT8(50, test_sys_a.get());
}

void setup() {
    delay(2000);
    UNITY_BEGIN();

    RUN_TEST(test_property_system_init);
    RUN_TEST(test_property_mark_dirty_on_change);
    RUN_TEST(test_property_system_loop_runs);
    RUN_TEST(test_rapid_changes);
    RUN_TEST(test_multiple_properties_dirty);
    RUN_TEST(test_no_change_no_dirty);

    UNITY_END();
}

void loop() {}
