#include <Arduino.h>
#include <unity.h>
#include <Property.h>
#include <PropertySystem.h>
#include <PropertyStorage.h>

using namespace MicroProto;
using PB = PropertyBase;

// Persistent test properties
Property<uint8_t> persist_u8("persist_u8", 50, PropertyLevel::LOCAL,
    "test", UIHints(), PB::PERSISTENT);
Property<int32_t> persist_i32("persist_i32", -100, PropertyLevel::LOCAL,
    "test", UIHints(), PB::PERSISTENT);
Property<float>   persist_f32("persist_f32", 1.5f, PropertyLevel::LOCAL,
    "test", UIHints(), PB::PERSISTENT);
Property<uint8_t> no_persist("no_persist", 77, PropertyLevel::LOCAL);

void setUp(void) {}
void tearDown(void) {}

void test_storage_init(void) {
    PropertyStorage::init();
    TEST_PASS();
}

void test_save_and_load_uint8(void) {
    persist_u8 = 200;
    TEST_ASSERT_TRUE(PropertyStorage::save(&persist_u8));

    persist_u8 = 0;
    TEST_ASSERT_EQUAL_UINT8(0, persist_u8.get());

    TEST_ASSERT_TRUE(PropertyStorage::load(&persist_u8));
    TEST_ASSERT_EQUAL_UINT8(200, persist_u8.get());
}

void test_save_and_load_int32(void) {
    persist_i32 = 123456;
    TEST_ASSERT_TRUE(PropertyStorage::save(&persist_i32));

    persist_i32 = 0;
    TEST_ASSERT_TRUE(PropertyStorage::load(&persist_i32));
    TEST_ASSERT_EQUAL_INT32(123456, persist_i32.get());
}

void test_save_and_load_float(void) {
    persist_f32 = 3.14159f;
    TEST_ASSERT_TRUE(PropertyStorage::save(&persist_f32));

    persist_f32 = 0.0f;
    TEST_ASSERT_TRUE(PropertyStorage::load(&persist_f32));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.14159f, persist_f32.get());
}

void test_erase_property(void) {
    persist_u8 = 111;
    PropertyStorage::save(&persist_u8);

    TEST_ASSERT_TRUE(PropertyStorage::erase(&persist_u8));

    persist_u8 = 0;
    TEST_ASSERT_FALSE(PropertyStorage::load(&persist_u8));
}

void test_save_load_all_persistent(void) {
    persist_u8 = 55;
    persist_i32 = 777;
    persist_f32 = 9.99f;
    no_persist = 222;

    PropertySystem::saveToStorage();

    persist_u8 = 0;
    persist_i32 = 0;
    persist_f32 = 0.0f;
    no_persist = 0;

    PropertySystem::loadFromStorage();

    TEST_ASSERT_EQUAL_UINT8(55, persist_u8.get());
    TEST_ASSERT_EQUAL_INT32(777, persist_i32.get());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 9.99f, persist_f32.get());

    // Non-persistent should remain 0
    TEST_ASSERT_EQUAL_UINT8(0, no_persist.get());
}

void test_erase_all(void) {
    persist_u8 = 11;
    persist_i32 = 22;
    PropertyStorage::save(&persist_u8);
    PropertyStorage::save(&persist_i32);

    TEST_ASSERT_TRUE(PropertyStorage::eraseAll());

    persist_u8 = 0;
    persist_i32 = 0;
    TEST_ASSERT_FALSE(PropertyStorage::load(&persist_u8));
    TEST_ASSERT_FALSE(PropertyStorage::load(&persist_i32));
}

void setup() {
    delay(2000);
    UNITY_BEGIN();

    RUN_TEST(test_storage_init);
    RUN_TEST(test_save_and_load_uint8);
    RUN_TEST(test_save_and_load_int32);
    RUN_TEST(test_save_and_load_float);
    RUN_TEST(test_erase_property);
    RUN_TEST(test_save_load_all_persistent);
    RUN_TEST(test_erase_all);

    UNITY_END();
}

void loop() {}
