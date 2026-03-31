#include <unity.h>
#include <array>
#include <cstring>
#include "StreamProperty.h"
#include "wire/Buffer.h"
#include "wire/TypeCodec.h"
#include "wire/PropertyUpdate.h"
#include "messages/Schema.h"
#include "Reflect.h"

using namespace MicroProto;

void setUp() {
    PropertyBase::byId.fill(nullptr);
    PropertyBase::count = 0;
}

void tearDown() {}

// ============== Basic struct for testing ==============

struct TestEvent {
    uint32_t timestamp;
    uint8_t  code;
    uint8_t  _pad1;
    uint8_t  _pad2;
    uint8_t  _pad3;
};

static_assert(sizeof(TestEvent) == 8, "TestEvent must be 8 bytes");

MICROPROTO_FIELD_NAMES(TestEvent, "timestamp", "code", "_pad1", "_pad2", "_pad3");

// ============== StreamProperty Core ==============

void test_stream_empty_on_creation() {
    StreamProperty<TestEvent, 4> stream("test", PropertyLevel::LOCAL);

    TEST_ASSERT_EQUAL(0, stream.count());
    TEST_ASSERT_TRUE(stream.empty());
    TEST_ASSERT_FALSE(stream.hasPending());
    TEST_ASSERT_EQUAL(TYPE_STREAM, stream.getTypeId());
    TEST_ASSERT_TRUE(stream.isStream());
    TEST_ASSERT_TRUE(stream.isContainer());
}

void test_stream_type_metadata() {
    StreamProperty<TestEvent, 4> stream("test", PropertyLevel::LOCAL);

    TEST_ASSERT_EQUAL(TYPE_OBJECT, stream.getElementTypeId());
    TEST_ASSERT_EQUAL(sizeof(TestEvent), stream.getElementSize());
    TEST_ASSERT_EQUAL(0, stream.getElementCount());
    TEST_ASSERT_EQUAL(4, stream.getMaxElementCount());
}

void test_stream_push_single() {
    StreamProperty<TestEvent, 4> stream("test", PropertyLevel::LOCAL);

    TestEvent e{100, 1, 0, 0, 0};
    stream.push(e);

    TEST_ASSERT_EQUAL(1, stream.count());
    TEST_ASSERT_FALSE(stream.empty());
    TEST_ASSERT_TRUE(stream.hasPending());

    TEST_ASSERT_EQUAL(100, stream[0].timestamp);
    TEST_ASSERT_EQUAL(1, stream[0].code);
}

void test_stream_push_multiple() {
    StreamProperty<TestEvent, 4> stream("test", PropertyLevel::LOCAL);

    for (uint8_t i = 0; i < 3; i++) {
        TestEvent e{static_cast<uint32_t>(i * 100), i, 0, 0, 0};
        stream.push(e);
    }

    TEST_ASSERT_EQUAL(3, stream.count());
    TEST_ASSERT_EQUAL(0, stream[0].timestamp);   // oldest
    TEST_ASSERT_EQUAL(100, stream[1].timestamp);
    TEST_ASSERT_EQUAL(200, stream[2].timestamp);  // newest
}

void test_stream_ring_buffer_wraps() {
    StreamProperty<TestEvent, 4> stream("test", PropertyLevel::LOCAL);

    // Push 6 entries into a buffer of 4
    for (uint8_t i = 0; i < 6; i++) {
        TestEvent e{static_cast<uint32_t>(i * 100), i, 0, 0, 0};
        stream.push(e);
    }

    // Buffer should contain the last 4 entries
    TEST_ASSERT_EQUAL(4, stream.count());
    TEST_ASSERT_EQUAL(200, stream[0].timestamp);   // oldest surviving
    TEST_ASSERT_EQUAL(300, stream[1].timestamp);
    TEST_ASSERT_EQUAL(400, stream[2].timestamp);
    TEST_ASSERT_EQUAL(500, stream[3].timestamp);   // newest
}

void test_stream_setdata_rejected() {
    StreamProperty<TestEvent, 4> stream("test", PropertyLevel::LOCAL);

    TestEvent e{100, 1, 0, 0, 0};
    stream.push(e);

    // setData should be a no-op (streams are append-only)
    TestEvent fake{999, 99, 0, 0, 0};
    stream.setData(&fake, sizeof(fake));

    TEST_ASSERT_EQUAL(1, stream.count());
    TEST_ASSERT_EQUAL(100, stream[0].timestamp);  // unchanged
}

// ============== Pending / Delta Tracking ==============

void test_stream_pending_tracks_new_entries() {
    StreamProperty<TestEvent, 4> stream("test", PropertyLevel::LOCAL);

    TestEvent e1{100, 1, 0, 0, 0};
    TestEvent e2{200, 2, 0, 0, 0};
    stream.push(e1);
    stream.push(e2);

    TEST_ASSERT_TRUE(stream.hasPending());
    TEST_ASSERT_EQUAL(2, stream.count());
}

void test_stream_clear_pending_resets() {
    StreamProperty<TestEvent, 4> stream("test", PropertyLevel::LOCAL);

    TestEvent e{100, 1, 0, 0, 0};
    stream.push(e);

    stream.clearPending();

    TEST_ASSERT_FALSE(stream.hasPending());
    TEST_ASSERT_EQUAL(1, stream.count());  // entry still in buffer
}

void test_stream_pending_after_clear_only_new() {
    StreamProperty<TestEvent, 4> stream("test", PropertyLevel::LOCAL);

    // Push 2, clear, push 1 more
    stream.push({100, 1, 0, 0, 0});
    stream.push({200, 2, 0, 0, 0});
    stream.clearPending();

    stream.push({300, 3, 0, 0, 0});

    // Pending should encode only the new entry
    uint8_t buf[128];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(stream.encodePendingEntries(wb));

    // Decode: varint count + entries
    ReadBuffer rb(buf, wb.position());
    uint32_t count = rb.readVarint();
    TEST_ASSERT_EQUAL(1, count);  // only 1 pending entry

    TestEvent decoded{};
    TEST_ASSERT_TRUE(rb.readBytes(reinterpret_cast<uint8_t*>(&decoded), sizeof(TestEvent)));
    TEST_ASSERT_EQUAL(300, decoded.timestamp);
    TEST_ASSERT_EQUAL(3, decoded.code);
}

// ============== Encoding ==============

void test_stream_encode_full_buffer() {
    StreamProperty<TestEvent, 4> stream("test", PropertyLevel::LOCAL);

    stream.push({100, 1, 0, 0, 0});
    stream.push({200, 2, 0, 0, 0});
    stream.push({300, 3, 0, 0, 0});

    uint8_t buf[256];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(stream.encodeFullBuffer(wb));

    ReadBuffer rb(buf, wb.position());
    uint32_t count = rb.readVarint();
    TEST_ASSERT_EQUAL(3, count);

    // Verify entries in order (oldest first)
    for (uint32_t i = 0; i < 3; i++) {
        TestEvent decoded{};
        TEST_ASSERT_TRUE(rb.readBytes(reinterpret_cast<uint8_t*>(&decoded), sizeof(TestEvent)));
        TEST_ASSERT_EQUAL((i + 1) * 100, decoded.timestamp);
        TEST_ASSERT_EQUAL(i + 1, decoded.code);
    }
}

void test_stream_encode_full_buffer_after_wrap() {
    StreamProperty<TestEvent, 3> stream("test", PropertyLevel::LOCAL);

    // Push 5 entries into buffer of 3
    for (uint8_t i = 1; i <= 5; i++) {
        stream.push({i * 100u, i, 0, 0, 0});
    }

    uint8_t buf[256];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(stream.encodeFullBuffer(wb));

    ReadBuffer rb(buf, wb.position());
    uint32_t count = rb.readVarint();
    TEST_ASSERT_EQUAL(3, count);

    // Should contain entries 3, 4, 5 (oldest surviving first)
    TestEvent decoded{};
    rb.readBytes(reinterpret_cast<uint8_t*>(&decoded), sizeof(TestEvent));
    TEST_ASSERT_EQUAL(300, decoded.timestamp);

    rb.readBytes(reinterpret_cast<uint8_t*>(&decoded), sizeof(TestEvent));
    TEST_ASSERT_EQUAL(400, decoded.timestamp);

    rb.readBytes(reinterpret_cast<uint8_t*>(&decoded), sizeof(TestEvent));
    TEST_ASSERT_EQUAL(500, decoded.timestamp);
}

void test_stream_encode_pending_entries() {
    StreamProperty<TestEvent, 4> stream("test", PropertyLevel::LOCAL);

    stream.push({100, 1, 0, 0, 0});
    stream.push({200, 2, 0, 0, 0});

    uint8_t buf[256];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(stream.encodePendingEntries(wb));

    ReadBuffer rb(buf, wb.position());
    uint32_t count = rb.readVarint();
    TEST_ASSERT_EQUAL(2, count);

    TestEvent decoded{};
    rb.readBytes(reinterpret_cast<uint8_t*>(&decoded), sizeof(TestEvent));
    TEST_ASSERT_EQUAL(100, decoded.timestamp);

    rb.readBytes(reinterpret_cast<uint8_t*>(&decoded), sizeof(TestEvent));
    TEST_ASSERT_EQUAL(200, decoded.timestamp);
}

void test_stream_encode_empty() {
    StreamProperty<TestEvent, 4> stream("test", PropertyLevel::LOCAL);

    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(stream.encodeFullBuffer(wb));

    ReadBuffer rb(buf, wb.position());
    uint32_t count = rb.readVarint();
    TEST_ASSERT_EQUAL(0, count);
}

void test_stream_typecodec_encodes_full() {
    StreamProperty<TestEvent, 4> stream("test", PropertyLevel::LOCAL);

    stream.push({100, 1, 0, 0, 0});
    stream.push({200, 2, 0, 0, 0});

    uint8_t buf[256];
    WriteBuffer wb(buf, sizeof(buf));

    // TypeCodec::encodeProperty should use encodeFullBuffer for streams
    TEST_ASSERT_TRUE(TypeCodec::encodeProperty(wb, &stream));

    ReadBuffer rb(buf, wb.position());
    uint32_t count = rb.readVarint();
    TEST_ASSERT_EQUAL(2, count);
}

void test_stream_typecodec_decode_rejected() {
    StreamProperty<TestEvent, 4> stream("test", PropertyLevel::LOCAL);

    // Streams are read-only — decodeProperty should return false
    uint8_t fakeData[] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    ReadBuffer rb(fakeData, sizeof(fakeData));
    TEST_ASSERT_FALSE(TypeCodec::decodeProperty(rb, &stream));
}

// ============== N=0 (Online-Only) ==============

void test_stream_n0_no_history_on_connect() {
    StreamProperty<TestEvent, 0> stream("test", PropertyLevel::LOCAL);

    stream.push({100, 1, 0, 0, 0});
    stream.push({200, 2, 0, 0, 0});

    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(stream.encodeFullBuffer(wb));

    // N=0: encodeFullBuffer should return count=0 (no history to replay)
    ReadBuffer rb(buf, wb.position());
    uint32_t count = rb.readVarint();
    TEST_ASSERT_EQUAL(0, count);
}

void test_stream_n0_pending_works() {
    StreamProperty<TestEvent, 0> stream("test", PropertyLevel::LOCAL);

    stream.push({100, 1, 0, 0, 0});
    stream.push({200, 2, 0, 0, 0});

    uint8_t buf[256];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(stream.encodePendingEntries(wb));

    ReadBuffer rb(buf, wb.position());
    uint32_t count = rb.readVarint();
    TEST_ASSERT_EQUAL(2, count);
}

void test_stream_n0_clear_pending_discards_buffer() {
    StreamProperty<TestEvent, 0> stream("test", PropertyLevel::LOCAL);

    stream.push({100, 1, 0, 0, 0});
    stream.clearPending();

    // For N=0, clearPending also clears the buffer entirely
    TEST_ASSERT_EQUAL(0, stream.count());
    TEST_ASSERT_FALSE(stream.hasPending());
}

void test_stream_n0_pending_buffer_wraps() {
    StreamProperty<TestEvent, 0> stream("test", PropertyLevel::LOCAL);

    // Default pending buffer is MICROPROTO_STREAM_PENDING_SIZE (8)
    // Push more than that
    for (uint8_t i = 0; i < 12; i++) {
        stream.push({i * 100u, i, 0, 0, 0});
    }

    // Buffer should contain last 8 entries (pending capped at buffer capacity)
    uint8_t buf[512];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(stream.encodePendingEntries(wb));

    ReadBuffer rb(buf, wb.position());
    uint32_t count = rb.readVarint();
    TEST_ASSERT_EQUAL(8, count);

    // First entry should be index 4 (entries 4-11)
    TestEvent decoded{};
    rb.readBytes(reinterpret_cast<uint8_t*>(&decoded), sizeof(TestEvent));
    TEST_ASSERT_EQUAL(400, decoded.timestamp);
}

// ============== Schema Encoding ==============

void test_stream_schema_encoding() {
    StreamProperty<TestEvent, 10> stream("events", PropertyLevel::LOCAL);

    uint8_t buf[256];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(stream.encodeTypeDefinition(wb));

    ReadBuffer rb(buf, wb.position());

    // TYPE_STREAM marker
    uint8_t typeId = rb.readByte();
    TEST_ASSERT_EQUAL(TYPE_STREAM, typeId);

    // History capacity
    uint32_t capacity = rb.readVarint();
    TEST_ASSERT_EQUAL(10, capacity);

    // Element type: OBJECT (TestEvent is a struct)
    uint8_t elemType = rb.readByte();
    TEST_ASSERT_EQUAL(TYPE_OBJECT, elemType);
}

void test_stream_schema_n0_capacity() {
    StreamProperty<TestEvent, 0> stream("events", PropertyLevel::LOCAL);

    uint8_t buf[256];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(stream.encodeTypeDefinition(wb));

    ReadBuffer rb(buf, wb.position());
    rb.readByte();  // TYPE_STREAM

    uint32_t capacity = rb.readVarint();
    TEST_ASSERT_EQUAL(0, capacity);  // N=0 advertised to clients
}

// ============== Basic Type Stream ==============

void test_stream_basic_type() {
    StreamProperty<int32_t, 4> stream("counters", PropertyLevel::LOCAL);

    stream.push(42);
    stream.push(100);
    stream.push(999);

    TEST_ASSERT_EQUAL(3, stream.count());
    TEST_ASSERT_EQUAL(TYPE_STREAM, stream.getTypeId());
    TEST_ASSERT_EQUAL(TYPE_INT32, stream.getElementTypeId());

    uint8_t buf[128];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(stream.encodeFullBuffer(wb));

    ReadBuffer rb(buf, wb.position());
    uint32_t count = rb.readVarint();
    TEST_ASSERT_EQUAL(3, count);

    TEST_ASSERT_EQUAL(42, rb.readInt32());
    TEST_ASSERT_EQUAL(100, rb.readInt32());
    TEST_ASSERT_EQUAL(999, rb.readInt32());
}

// ============== Property Registration ==============

void test_stream_registers_in_property_system() {
    StreamProperty<TestEvent, 4> stream("events", PropertyLevel::LOCAL,
        "Test events", UIHints(), false, true);  // readonly

    TEST_ASSERT_EQUAL(1, PropertyBase::count);
    TEST_ASSERT_NOT_NULL(PropertyBase::find(stream.id));
    TEST_ASSERT_TRUE(PropertyBase::find(stream.id)->isStream());
    TEST_ASSERT_TRUE(PropertyBase::find(stream.id)->readonly);
}

void test_stream_full_schema_encode() {
    StreamProperty<TestEvent, 4> stream("events", PropertyLevel::LOCAL,
        "Test events",
        UIHints().setWidget(Widget::Stream::LOG).setColor(UIColor::ROSE),
        false, true);  // readonly

    uint8_t buf[512];
    WriteBuffer wb(buf, sizeof(buf));

    // Encode full schema (single property)
    TEST_ASSERT_TRUE(SchemaEncoder::encodeProperty(wb, &stream));
    TEST_ASSERT_TRUE(wb.position() > 0);
}

// ============== Runner ==============

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // Core
    RUN_TEST(test_stream_empty_on_creation);
    RUN_TEST(test_stream_type_metadata);
    RUN_TEST(test_stream_push_single);
    RUN_TEST(test_stream_push_multiple);
    RUN_TEST(test_stream_ring_buffer_wraps);
    RUN_TEST(test_stream_setdata_rejected);

    // Pending/delta
    RUN_TEST(test_stream_pending_tracks_new_entries);
    RUN_TEST(test_stream_clear_pending_resets);
    RUN_TEST(test_stream_pending_after_clear_only_new);

    // Encoding
    RUN_TEST(test_stream_encode_full_buffer);
    RUN_TEST(test_stream_encode_full_buffer_after_wrap);
    RUN_TEST(test_stream_encode_pending_entries);
    RUN_TEST(test_stream_encode_empty);
    RUN_TEST(test_stream_typecodec_encodes_full);
    RUN_TEST(test_stream_typecodec_decode_rejected);

    // N=0 online-only
    RUN_TEST(test_stream_n0_no_history_on_connect);
    RUN_TEST(test_stream_n0_pending_works);
    RUN_TEST(test_stream_n0_clear_pending_discards_buffer);
    RUN_TEST(test_stream_n0_pending_buffer_wraps);

    // Schema
    RUN_TEST(test_stream_schema_encoding);
    RUN_TEST(test_stream_schema_n0_capacity);

    // Basic type stream
    RUN_TEST(test_stream_basic_type);

    // Property registration
    RUN_TEST(test_stream_registers_in_property_system);
    RUN_TEST(test_stream_full_schema_encode);

    return UNITY_END();
}
