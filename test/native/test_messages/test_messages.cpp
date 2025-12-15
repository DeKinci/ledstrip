#ifdef NATIVE_TEST

#include <unity.h>
#include <wire/Buffer.h>
#include <wire/OpCode.h>
#include <messages/Hello.h>
#include <messages/Error.h>
#include <cstring>

using namespace MicroProto;

void setUp(void) {}
void tearDown(void) {}

// ==== HELLO Tests ====

void test_hello_request_encode() {
    uint8_t buf[16];
    WriteBuffer wb(buf, sizeof(buf));

    HelloRequest hello;
    hello.protocolVersion = 1;
    hello.maxPacketSize = 4096;
    hello.deviceId = 0x12345678;

    TEST_ASSERT_TRUE(hello.encode(wb));
    TEST_ASSERT_EQUAL(HelloRequest::encodedSize(), wb.position());

    // Verify bytes
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[0]);  // HELLO opcode
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[1]);  // version
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[2]);  // maxPacketSize low
    TEST_ASSERT_EQUAL_HEX8(0x10, buf[3]);  // maxPacketSize high (4096 = 0x1000)
    // deviceId little-endian
    TEST_ASSERT_EQUAL_HEX8(0x78, buf[4]);
    TEST_ASSERT_EQUAL_HEX8(0x56, buf[5]);
    TEST_ASSERT_EQUAL_HEX8(0x34, buf[6]);
    TEST_ASSERT_EQUAL_HEX8(0x12, buf[7]);
}

void test_hello_request_decode() {
    uint8_t data[] = {
        0x00,              // HELLO opcode
        0x01,              // version
        0x00, 0x10,        // maxPacketSize = 4096
        0x78, 0x56, 0x34, 0x12  // deviceId
    };

    ReadBuffer rb(data, sizeof(data));
    HelloRequest hello;

    TEST_ASSERT_TRUE(HelloRequest::decode(rb, hello));
    TEST_ASSERT_EQUAL(1, hello.protocolVersion);
    TEST_ASSERT_EQUAL(4096, hello.maxPacketSize);
    TEST_ASSERT_EQUAL_HEX32(0x12345678, hello.deviceId);
}

void test_hello_request_roundtrip() {
    uint8_t buf[16];

    HelloRequest original;
    original.protocolVersion = 1;
    original.maxPacketSize = 8192;
    original.deviceId = 0xDEADBEEF;

    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(original.encode(wb));

    ReadBuffer rb(buf, wb.position());
    HelloRequest decoded;
    TEST_ASSERT_TRUE(HelloRequest::decode(rb, decoded));

    TEST_ASSERT_EQUAL(original.protocolVersion, decoded.protocolVersion);
    TEST_ASSERT_EQUAL(original.maxPacketSize, decoded.maxPacketSize);
    TEST_ASSERT_EQUAL(original.deviceId, decoded.deviceId);
}

void test_hello_response_encode() {
    uint8_t buf[16];
    WriteBuffer wb(buf, sizeof(buf));

    HelloResponse hello;
    hello.protocolVersion = 1;
    hello.maxPacketSize = 4096;
    hello.sessionId = 0xABCD1234;
    hello.serverTimestamp = 1700000000;

    TEST_ASSERT_TRUE(hello.encode(wb));
    TEST_ASSERT_EQUAL(HelloResponse::encodedSize(), wb.position());
}

void test_hello_response_roundtrip() {
    uint8_t buf[16];

    HelloResponse original;
    original.protocolVersion = 1;
    original.maxPacketSize = 2048;
    original.sessionId = 0x11223344;
    original.serverTimestamp = 1700000000;

    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(original.encode(wb));

    ReadBuffer rb(buf, wb.position());
    HelloResponse decoded;
    TEST_ASSERT_TRUE(HelloResponse::decode(rb, decoded));

    TEST_ASSERT_EQUAL(original.protocolVersion, decoded.protocolVersion);
    TEST_ASSERT_EQUAL(original.maxPacketSize, decoded.maxPacketSize);
    TEST_ASSERT_EQUAL(original.sessionId, decoded.sessionId);
    TEST_ASSERT_EQUAL(original.serverTimestamp, decoded.serverTimestamp);
}

// ==== ERROR Tests ====

void test_error_encode_simple() {
    uint8_t buf[64];
    WriteBuffer wb(buf, sizeof(buf));

    ErrorMessage err(ErrorCode::VALIDATION_FAILED, "Bad value");
    TEST_ASSERT_TRUE(err.encode(wb));

    // Check header
    TEST_ASSERT_EQUAL_HEX8(0x07, buf[0]);  // ERROR opcode

    // Check error code (little-endian)
    TEST_ASSERT_EQUAL_HEX8(0x05, buf[1]);  // VALIDATION_FAILED = 0x0005
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[2]);

    // Message length (varint)
    TEST_ASSERT_EQUAL(9, buf[3]);  // "Bad value" = 9 chars

    // Message
    TEST_ASSERT_EQUAL_MEMORY("Bad value", buf + 4, 9);
}

void test_error_encode_with_related_opcode() {
    uint8_t buf[64];
    WriteBuffer wb(buf, sizeof(buf));

    ErrorMessage err = ErrorMessage::invalidOpcode(0x0F);
    TEST_ASSERT_TRUE(err.encode(wb));

    // Header should have flag set
    OpHeader header = OpHeader::decode(buf[0]);
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(OpCode::ERROR), header.opcode);
    TEST_ASSERT_EQUAL(ERROR_FLAG_HAS_RELATED_OPCODE, header.flags & ERROR_FLAG_HAS_RELATED_OPCODE);
}

void test_error_decode_simple() {
    uint8_t data[] = {
        0x07,              // ERROR opcode, no flags
        0x05, 0x00,        // VALIDATION_FAILED
        0x04,              // message length = 4
        'T', 'e', 's', 't' // message
    };

    ReadBuffer rb(data, sizeof(data));
    ErrorMessage err;

    TEST_ASSERT_TRUE(ErrorMessage::decode(rb, err));
    TEST_ASSERT_EQUAL(ErrorCode::VALIDATION_FAILED, err.code);
    TEST_ASSERT_EQUAL(4, err.messageLen);
    TEST_ASSERT_EQUAL_MEMORY("Test", err.message, 4);
    TEST_ASSERT_FALSE(err.hasRelatedOpcode);
}

void test_error_decode_with_related_opcode() {
    uint8_t data[] = {
        0x17,              // ERROR opcode, flags=1 (has_related_opcode)
        0x01, 0x00,        // INVALID_OPCODE
        0x00,              // message length = 0
        0x0F               // related opcode
    };

    ReadBuffer rb(data, sizeof(data));
    ErrorMessage err;

    TEST_ASSERT_TRUE(ErrorMessage::decode(rb, err));
    TEST_ASSERT_EQUAL(ErrorCode::INVALID_OPCODE, err.code);
    TEST_ASSERT_TRUE(err.hasRelatedOpcode);
    TEST_ASSERT_EQUAL(0x0F, err.relatedOpcode);
}

void test_error_roundtrip() {
    uint8_t buf[64];

    ErrorMessage original(ErrorCode::TYPE_MISMATCH, "Expected int32");

    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(original.encode(wb));

    ReadBuffer rb(buf, wb.position());
    ErrorMessage decoded;
    TEST_ASSERT_TRUE(ErrorMessage::decode(rb, decoded));

    TEST_ASSERT_EQUAL(original.code, decoded.code);
    TEST_ASSERT_EQUAL(original.messageLen, decoded.messageLen);
    TEST_ASSERT_EQUAL_MEMORY(original.message, decoded.message, original.messageLen);
}

void test_error_empty_message() {
    uint8_t buf[16];
    WriteBuffer wb(buf, sizeof(buf));

    ErrorMessage err(ErrorCode::SUCCESS);
    TEST_ASSERT_TRUE(err.encode(wb));

    ReadBuffer rb(buf, wb.position());
    ErrorMessage decoded;
    TEST_ASSERT_TRUE(ErrorMessage::decode(rb, decoded));

    TEST_ASSERT_EQUAL(ErrorCode::SUCCESS, decoded.code);
    TEST_ASSERT_EQUAL(0, decoded.messageLen);
}

// ==== OpHeader Tests (additional) ====

void test_error_opcode_header() {
    OpHeader h(OpCode::ERROR, 0, false);
    TEST_ASSERT_EQUAL_HEX8(0x07, h.encode());
}

void test_schema_opcode_header() {
    OpHeader h(OpCode::SCHEMA_UPSERT, 0, true);
    TEST_ASSERT_EQUAL_HEX8(0x83, h.encode());  // batch + opcode 3
}

// ==== Main ====

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // HELLO tests
    RUN_TEST(test_hello_request_encode);
    RUN_TEST(test_hello_request_decode);
    RUN_TEST(test_hello_request_roundtrip);
    RUN_TEST(test_hello_response_encode);
    RUN_TEST(test_hello_response_roundtrip);

    // ERROR tests
    RUN_TEST(test_error_encode_simple);
    RUN_TEST(test_error_encode_with_related_opcode);
    RUN_TEST(test_error_decode_simple);
    RUN_TEST(test_error_decode_with_related_opcode);
    RUN_TEST(test_error_roundtrip);
    RUN_TEST(test_error_empty_message);

    // OpHeader tests
    RUN_TEST(test_error_opcode_header);
    RUN_TEST(test_schema_opcode_header);

    return UNITY_END();
}

#endif // NATIVE_TEST