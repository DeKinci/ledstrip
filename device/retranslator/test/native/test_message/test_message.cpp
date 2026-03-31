#include <unity.h>
#include "message.h"

void test_location_encode_decode() {
    Message msg = Message::createLocation(0x42, 1, 1000, 3, 7);

    uint8_t buf[64];
    size_t len = msg.encode(buf, sizeof(buf));

    TEST_ASSERT_EQUAL(MESSAGE_HEADER_SIZE + 2, len);
    TEST_ASSERT_EQUAL(0x42, buf[0]); // sender_id
    TEST_ASSERT_EQUAL(0x01, buf[7]); // msg_type = Location

    Message decoded;
    TEST_ASSERT_TRUE(decoded.decode(buf, len));
    TEST_ASSERT_EQUAL(0x42, decoded.senderId);
    TEST_ASSERT_EQUAL(1, decoded.seq);
    TEST_ASSERT_EQUAL(1000, decoded.timestamp);
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(MsgType::Location), static_cast<uint8_t>(decoded.msgType));
    TEST_ASSERT_EQUAL(3, decoded.locationNodeA());
    TEST_ASSERT_EQUAL(7, decoded.locationNodeB());
}

void test_location_at_node() {
    Message msg = Message::createLocation(0x01, 1, 2000, 5);

    uint8_t buf[64];
    size_t len = msg.encode(buf, sizeof(buf));

    Message decoded;
    TEST_ASSERT_TRUE(decoded.decode(buf, len));
    TEST_ASSERT_EQUAL(5, decoded.locationNodeA());
    TEST_ASSERT_EQUAL(0xFF, decoded.locationNodeB());
}

void test_text_encode_decode() {
    const uint8_t text[] = "Hello tunnel!";
    Message msg = Message::createText(0x10, 1, 3000, text, sizeof(text) - 1);

    uint8_t buf[64];
    size_t len = msg.encode(buf, sizeof(buf));

    TEST_ASSERT_EQUAL(MESSAGE_HEADER_SIZE + 1 + 13, len);

    Message decoded;
    TEST_ASSERT_TRUE(decoded.decode(buf, len));
    TEST_ASSERT_EQUAL(0x10, decoded.senderId);
    TEST_ASSERT_EQUAL(1, decoded.seq);
    TEST_ASSERT_EQUAL(3000, decoded.timestamp);
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(MsgType::Text), static_cast<uint8_t>(decoded.msgType));
    TEST_ASSERT_EQUAL(13, decoded.textLength());
    TEST_ASSERT_EQUAL_MEMORY("Hello tunnel!", decoded.textData(), 13);
}

void test_beacon_encode_decode() {
    Message msg = Message::createBeacon(0x01, 0xABCD, 1);

    uint8_t buf[64];
    size_t len = msg.encode(buf, sizeof(buf));

    TEST_ASSERT_EQUAL(MESSAGE_HEADER_SIZE + 3, len);

    Message decoded;
    TEST_ASSERT_TRUE(decoded.decode(buf, len));
    TEST_ASSERT_EQUAL(0x01, decoded.senderId);
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(MsgType::Beacon), static_cast<uint8_t>(decoded.msgType));
    TEST_ASSERT_EQUAL(0xABCD, decoded.beaconStateHash());
    TEST_ASSERT_EQUAL(1, decoded.beaconNodeType());
}

void test_decode_too_short() {
    uint8_t buf[3] = {0x00, 0x01, 0x02};
    Message msg;
    TEST_ASSERT_FALSE(msg.decode(buf, 3));
}

void test_decode_invalid_location_payload() {
    Message orig = Message::createLocation(0x01, 1, 1000, 1, 2);
    uint8_t buf[64];
    orig.encode(buf, sizeof(buf));

    // Truncate payload to 1 byte (location needs 2)
    TEST_ASSERT_FALSE(Message().decode(buf, MESSAGE_HEADER_SIZE + 1));
}

void test_decode_invalid_text_payload() {
    // Manually craft a text message with wrong length byte
    uint8_t buf[] = {
        0x10,              // sender_id
        0x00, 0x01,        // seq
        0x00, 0x00, 0x0B, 0xB8, // timestamp
        0x02,              // msg_type = Text
        0x05,              // length byte says 5...
        'H', 'i',          // ...but only 2 bytes of data
    };

    Message msg;
    TEST_ASSERT_FALSE(msg.decode(buf, sizeof(buf)));
}

void test_to_entry_and_back() {
    const uint8_t text[] = "Test";
    Message orig = Message::createText(0x05, 42, 5000, text, 4);

    MessageEntry entry = orig.toEntry();
    TEST_ASSERT_EQUAL(0x05, entry.senderId);
    TEST_ASSERT_EQUAL(42, entry.seq);
    TEST_ASSERT_EQUAL(5000, entry.timestamp);
    TEST_ASSERT_TRUE(entry.valid);

    Message restored = Message::fromEntry(entry);
    TEST_ASSERT_EQUAL(orig.senderId, restored.senderId);
    TEST_ASSERT_EQUAL(orig.seq, restored.seq);
    TEST_ASSERT_EQUAL(orig.timestamp, restored.timestamp);
    TEST_ASSERT_EQUAL(orig.payloadLen, restored.payloadLen);
    TEST_ASSERT_EQUAL_MEMORY(orig.payload, restored.payload, orig.payloadLen);
}

void test_encode_buffer_too_small() {
    Message msg = Message::createLocation(0x01, 1, 1000, 1, 2);
    uint8_t buf[4]; // Too small for header + payload
    size_t len = msg.encode(buf, sizeof(buf));
    TEST_ASSERT_EQUAL(0, len);
}

void test_timestamp_encoding() {
    // Test large timestamp value (2025-01-01 ~= 1735689600)
    uint32_t ts = 1735689600;
    Message msg = Message::createLocation(0x01, 1, ts, 1, 2);

    uint8_t buf[64];
    size_t len = msg.encode(buf, sizeof(buf));

    Message decoded;
    decoded.decode(buf, len);
    TEST_ASSERT_EQUAL(ts, decoded.timestamp);
}

void test_seq_encoding() {
    Message msg = Message::createLocation(0x01, 0x1234, 1000, 1, 2);

    uint8_t buf[64];
    msg.encode(buf, sizeof(buf));

    Message decoded;
    decoded.decode(buf, MESSAGE_HEADER_SIZE + 2);
    TEST_ASSERT_EQUAL(0x1234, decoded.seq);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_location_encode_decode);
    RUN_TEST(test_location_at_node);
    RUN_TEST(test_text_encode_decode);
    RUN_TEST(test_beacon_encode_decode);
    RUN_TEST(test_decode_too_short);
    RUN_TEST(test_decode_invalid_location_payload);
    RUN_TEST(test_decode_invalid_text_payload);
    RUN_TEST(test_to_entry_and_back);
    RUN_TEST(test_encode_buffer_too_small);
    RUN_TEST(test_timestamp_encoding);
    RUN_TEST(test_seq_encoding);
    return UNITY_END();
}
