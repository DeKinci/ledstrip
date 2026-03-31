#include <Arduino.h>
#include <unity.h>
#include <StreamProperty.h>
#include <PropertySystem.h>
#include <PropertyStorage.h>
#include <wire/Buffer.h>
#include <wire/TypeCodec.h>

using namespace MicroProto;
using PB = PropertyBase;

// Test struct
struct TestEvent {
    uint32_t timestamp;
    uint8_t  code;
    uint8_t  _pad1;
    uint8_t  _pad2;
    uint8_t  _pad3;
};

// Non-persistent stream (online-only)
StreamProperty<TestEvent, 0> onlineStream("test_online",
    PropertyLevel::LOCAL, "Online test", UIHints(),
    PB::NOT_PERSISTENT, PB::READONLY);

// Persistent stream (ring buffer with NVS)
StreamProperty<TestEvent, 4> persistStream("test_persist",
    PropertyLevel::LOCAL, "Persist test", UIHints(),
    PB::PERSISTENT, PB::READONLY);

void setUp(void) {}
void tearDown(void) {}

void test_stream_registers(void) {
    TEST_ASSERT_NOT_NULL(PropertyBase::find(onlineStream.id));
    TEST_ASSERT_NOT_NULL(PropertyBase::find(persistStream.id));
    TEST_ASSERT_TRUE(PropertyBase::find(onlineStream.id)->isStream());
    TEST_ASSERT_TRUE(PropertyBase::find(persistStream.id)->isStream());
    TEST_ASSERT_EQUAL(TYPE_STREAM, onlineStream.getTypeId());
}

void test_stream_push_and_read(void) {
    TestEvent e{millis(), 42, 0, 0, 0};
    persistStream.push(e);

    TEST_ASSERT_EQUAL(1, persistStream.count());
    TEST_ASSERT_EQUAL(42, persistStream[0].code);
}

void test_stream_ring_buffer_wraps(void) {
    // persistStream has capacity 4, push 6 entries
    for (uint8_t i = 0; i < 6; i++) {
        TestEvent e{millis(), i, 0, 0, 0};
        persistStream.push(e);
    }

    TEST_ASSERT_EQUAL(4, persistStream.count());
    // Oldest surviving entry should be code=2
    TEST_ASSERT_EQUAL(2, persistStream[0].code);
    TEST_ASSERT_EQUAL(5, persistStream[3].code);
}

void test_stream_encode_full(void) {
    // Clear by creating fresh — we can't clear, so just push known values
    // Push exactly 3 entries
    for (uint8_t i = 10; i < 13; i++) {
        persistStream.push({millis(), i, 0, 0, 0});
    }

    uint8_t buf[512];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(persistStream.encodeFullBuffer(wb));
    TEST_ASSERT_TRUE(wb.position() > 0);

    // Decode count
    ReadBuffer rb(buf, wb.position());
    uint32_t count = rb.readVarint();
    TEST_ASSERT_EQUAL(4, count);  // capacity is 4, buffer is full
}

void test_stream_n0_no_history(void) {
    onlineStream.push({millis(), 1, 0, 0, 0});
    onlineStream.push({millis(), 2, 0, 0, 0});

    uint8_t buf[64];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(onlineStream.encodeFullBuffer(wb));

    ReadBuffer rb(buf, wb.position());
    uint32_t count = rb.readVarint();
    TEST_ASSERT_EQUAL(0, count);  // N=0 means no history replay
}

void test_stream_pending_delta(void) {
    onlineStream.clearPending();

    onlineStream.push({millis(), 77, 0, 0, 0});

    uint8_t buf[128];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(onlineStream.encodePendingEntries(wb));

    ReadBuffer rb(buf, wb.position());
    uint32_t count = rb.readVarint();
    TEST_ASSERT_EQUAL(1, count);

    TestEvent decoded{};
    TEST_ASSERT_TRUE(rb.readBytes(reinterpret_cast<uint8_t*>(&decoded), sizeof(TestEvent)));
    TEST_ASSERT_EQUAL(77, decoded.code);
}

void test_stream_clear_pending(void) {
    onlineStream.push({millis(), 1, 0, 0, 0});
    TEST_ASSERT_TRUE(onlineStream.hasPending());

    onlineStream.clearPending();
    TEST_ASSERT_FALSE(onlineStream.hasPending());
    TEST_ASSERT_EQUAL(0, onlineStream.count());  // N=0: buffer cleared
}

void test_stream_nvs_persistence(void) {
    PropertyStorage::init();

    // Push known values
    for (uint8_t i = 0; i < 3; i++) {
        persistStream.push({1000u + i, i, 0, 0, 0});
    }

    // Save to NVS
    TEST_ASSERT_TRUE(persistStream.saveToNVS());

    // Remember what we had
    size_t countBefore = persistStream.count();
    uint8_t firstCode = persistStream[0].code;

    // Trash the in-memory state by pushing garbage
    for (int i = 0; i < 4; i++) {
        persistStream.push({99999, 99, 0, 0, 0});
    }

    // Load from NVS
    TEST_ASSERT_TRUE(persistStream.loadFromNVS());
    TEST_ASSERT_EQUAL(countBefore, persistStream.count());
    TEST_ASSERT_EQUAL(firstCode, persistStream[0].code);
}

void test_stream_typecodec_rejects_decode(void) {
    uint8_t fakeData[] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    ReadBuffer rb(fakeData, sizeof(fakeData));
    TEST_ASSERT_FALSE(TypeCodec::decodeProperty(rb, &onlineStream));
}

void test_stream_setdata_noop(void) {
    size_t before = persistStream.count();
    TestEvent fake{0, 0, 0, 0, 0};
    persistStream.setData(&fake, sizeof(fake));
    TEST_ASSERT_EQUAL(before, persistStream.count());  // unchanged
}

void setup() {
    delay(2000);
    PropertyStorage::init();

    UNITY_BEGIN();

    RUN_TEST(test_stream_registers);
    RUN_TEST(test_stream_push_and_read);
    RUN_TEST(test_stream_ring_buffer_wraps);
    RUN_TEST(test_stream_encode_full);
    RUN_TEST(test_stream_n0_no_history);
    RUN_TEST(test_stream_pending_delta);
    RUN_TEST(test_stream_clear_pending);
    RUN_TEST(test_stream_nvs_persistence);
    RUN_TEST(test_stream_typecodec_rejects_decode);
    RUN_TEST(test_stream_setdata_noop);

    UNITY_END();
}

void loop() {}
