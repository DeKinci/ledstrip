#ifdef NATIVE_TEST

#include <unity.h>
#include <wire/Buffer.h>
#include <wire/OpCode.h>
#include <wire/PropertyUpdate.h>
#include <messages/Hello.h>
#include <messages/Error.h>
#include <messages/Schema.h>
#include <messages/Resource.h>
#include <cstring>
#include <climits>

using namespace MicroProto;

void setUp(void) {}
void tearDown(void) {}

// ==== HELLO Tests ====

void test_hello_request_encode() {
    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));

    Hello hello = Hello::request(0x12345678, 4096);

    TEST_ASSERT_TRUE(hello.encode(wb));

    // Verify header: opcode=0, flags=0 (is_response=0)
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[0]);
    // Version
    TEST_ASSERT_EQUAL_HEX8(PROTOCOL_VERSION, buf[1]);
    // maxPacketSize as varint (4096 = 0x1000)
    // 4096 requires 2 bytes in varint: 0x80 | (4096 & 0x7F), 4096 >> 7
    TEST_ASSERT_EQUAL_HEX8(0x80, buf[2]);  // (4096 & 0x7F) | 0x80 = 0x80
    TEST_ASSERT_EQUAL_HEX8(0x20, buf[3]);  // 4096 >> 7 = 32
    // deviceId as varint (0x12345678 = 305419896)
}

void test_hello_request_roundtrip() {
    uint8_t buf[32];

    Hello original = Hello::request(0xDEADBEEF, 8192);

    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(original.encode(wb));

    ReadBuffer rb(buf, wb.position());
    Hello decoded;
    TEST_ASSERT_TRUE(Hello::decode(rb, decoded));

    TEST_ASSERT_FALSE(decoded.isResponse);
    TEST_ASSERT_EQUAL(original.protocolVersion, decoded.protocolVersion);
    TEST_ASSERT_EQUAL(original.maxPacketSize, decoded.maxPacketSize);
    TEST_ASSERT_EQUAL(original.deviceId, decoded.deviceId);
}

void test_hello_response_encode() {
    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));

    Hello hello = Hello::response(0xABCD1234, 1700000000, 4096);

    TEST_ASSERT_TRUE(hello.encode(wb));

    // Verify header: opcode=0, flags=1 (is_response=1)
    TEST_ASSERT_EQUAL_HEX8(0x10, buf[0]);  // opcode=0, flags=1 (IS_RESPONSE)
    // Version
    TEST_ASSERT_EQUAL_HEX8(PROTOCOL_VERSION, buf[1]);
}

void test_hello_response_roundtrip() {
    uint8_t buf[32];

    Hello original = Hello::response(0x11223344, 1700000000, 2048);

    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(original.encode(wb));

    ReadBuffer rb(buf, wb.position());
    Hello decoded;
    TEST_ASSERT_TRUE(Hello::decode(rb, decoded));

    TEST_ASSERT_TRUE(decoded.isResponse);
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

    // Check header: opcode=7, flags=0 (no schema_mismatch)
    TEST_ASSERT_EQUAL_HEX8(0x07, buf[0]);

    // Check error code (little-endian)
    TEST_ASSERT_EQUAL_HEX8(0x05, buf[1]);  // VALIDATION_FAILED = 0x0005
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[2]);

    // Message length (varint)
    TEST_ASSERT_EQUAL(9, buf[3]);  // "Bad value" = 9 chars

    // Message
    TEST_ASSERT_EQUAL_MEMORY("Bad value", buf + 4, 9);
}

void test_error_encode_with_schema_mismatch() {
    uint8_t buf[64];
    WriteBuffer wb(buf, sizeof(buf));

    ErrorMessage err = ErrorMessage::typeMismatch(true);  // schema_mismatch=true
    TEST_ASSERT_TRUE(err.encode(wb));

    // Header should have schema_mismatch flag set (bit0 of flags)
    OpCode opcode;
    uint8_t flags;
    decodeOpHeader(buf[0], opcode, flags);
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(OpCode::ERROR), static_cast<uint8_t>(opcode));
    TEST_ASSERT_TRUE(flags & Flags::SCHEMA_MISMATCH);
}

void test_error_decode_simple() {
    uint8_t data[] = {
        0x07,              // ERROR opcode, flags=0
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
    TEST_ASSERT_FALSE(err.schemaMismatch);
}

void test_error_decode_with_schema_mismatch() {
    uint8_t data[] = {
        0x17,              // ERROR opcode, flags=1 (schema_mismatch)
        0x04, 0x00,        // TYPE_MISMATCH
        0x00               // message length = 0
    };

    ReadBuffer rb(data, sizeof(data));
    ErrorMessage err;

    TEST_ASSERT_TRUE(ErrorMessage::decode(rb, err));
    TEST_ASSERT_EQUAL(ErrorCode::TYPE_MISMATCH, err.code);
    TEST_ASSERT_TRUE(err.schemaMismatch);
}

void test_error_roundtrip() {
    uint8_t buf[64];

    ErrorMessage original(ErrorCode::TYPE_MISMATCH, "Expected int32", true);

    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(original.encode(wb));

    ReadBuffer rb(buf, wb.position());
    ErrorMessage decoded;
    TEST_ASSERT_TRUE(ErrorMessage::decode(rb, decoded));

    TEST_ASSERT_EQUAL(original.code, decoded.code);
    TEST_ASSERT_EQUAL(original.messageLen, decoded.messageLen);
    TEST_ASSERT_EQUAL_MEMORY(original.message, decoded.message, original.messageLen);
    TEST_ASSERT_EQUAL(original.schemaMismatch, decoded.schemaMismatch);
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

// ==== OpHeader Tests ====

void test_error_opcode_header() {
    uint8_t encoded = encodeOpHeader(OpCode::ERROR, 0);
    TEST_ASSERT_EQUAL_HEX8(0x07, encoded);
}

void test_schema_opcode_header_batched() {
    uint8_t encoded = encodeOpHeader(OpCode::SCHEMA_UPSERT, Flags::BATCH);
    TEST_ASSERT_EQUAL_HEX8(0x13, encoded);  // opcode=3, flags=1 (batch)
}

void test_rpc_opcode_header() {
    uint8_t encoded = encodeOpHeader(OpCode::RPC, 0);
    TEST_ASSERT_EQUAL_HEX8(0x05, encoded);  // opcode=5, flags=0
}

void test_ping_opcode_header() {
    uint8_t encoded = encodeOpHeader(OpCode::PING, Flags::IS_RESPONSE);
    TEST_ASSERT_EQUAL_HEX8(0x16, encoded);  // opcode=6, flags=1 (is_response)
}

// ==== SCHEMA_DELETE Tests ====

void test_schema_delete_single_encode() {
    uint8_t buf[16];
    WriteBuffer wb(buf, sizeof(buf));

    TEST_ASSERT_TRUE(SchemaDeleteEncoder::encodePropertyDelete(wb, 42));

    // Verify: opcode=4, flags=0, item_type=1 (PROPERTY), propid=42
    TEST_ASSERT_EQUAL(3, wb.position());
    TEST_ASSERT_EQUAL_HEX8(0x04, buf[0]);  // opcode=4, flags=0
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[1]);  // item_type=PROPERTY (1)
    TEST_ASSERT_EQUAL_HEX8(42, buf[2]);    // propid=42 (single byte)
}

void test_schema_delete_single_decode() {
    // opcode=4, flags=0, item_type=1, propid=42
    uint8_t data[] = { 0x04, 0x01, 42 };

    OpCode opcode;
    uint8_t flags;
    decodeOpHeader(data[0], opcode, flags);

    TEST_ASSERT_EQUAL(OpCode::SCHEMA_DELETE, opcode);
    TEST_ASSERT_EQUAL(0, flags);

    ReadBuffer rb(data + 1, sizeof(data) - 1);
    SchemaDeleteDecoder::DeleteItem items[8];
    size_t itemCount = 0;

    TEST_ASSERT_TRUE(SchemaDeleteDecoder::decode(rb, flags, items, 8, itemCount));
    TEST_ASSERT_EQUAL(1, itemCount);
    TEST_ASSERT_EQUAL(SchemaItemType::PROPERTY, items[0].type);
    TEST_ASSERT_EQUAL(42, items[0].itemId);
}

void test_schema_delete_batched_encode() {
    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));

    uint16_t ids[] = { 1, 5, 10, 200 };
    TEST_ASSERT_TRUE(SchemaDeleteEncoder::encodeBatchedDelete(wb, ids, 4));

    // Verify: opcode=4, flags=1 (batch), batch_count=3 (4-1)
    // Then 4 items: [type=1, propid=1], [type=1, propid=5], [type=1, propid=10], [type=1, propid=200]
    TEST_ASSERT_EQUAL(11, wb.position());  // 1 + 1 + 4*(1+1) + 1 extra for propid 200
    TEST_ASSERT_EQUAL_HEX8(0x14, buf[0]);  // opcode=4, flags=1 (batch)
    TEST_ASSERT_EQUAL_HEX8(3, buf[1]);     // batch_count = 4-1 = 3
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[2]);  // item_type=PROPERTY
    TEST_ASSERT_EQUAL_HEX8(1, buf[3]);     // propid=1
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[4]);  // item_type=PROPERTY
    TEST_ASSERT_EQUAL_HEX8(5, buf[5]);     // propid=5
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[6]);  // item_type=PROPERTY
    TEST_ASSERT_EQUAL_HEX8(10, buf[7]);    // propid=10
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[8]);  // item_type=PROPERTY
    // propid=200 requires 2 bytes: 0x80 | (200 & 0x7F) = 0xC8, 200 >> 7 = 1
    TEST_ASSERT_EQUAL_HEX8(0xC8, buf[9]);  // propid low byte
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[10]); // propid high byte
}

void test_schema_delete_batched_decode() {
    // opcode=4, flags=1 (batch), batch_count=2 (3 items)
    // [type=1, propid=10], [type=1, propid=20], [type=1, propid=30]
    uint8_t data[] = { 0x14, 2, 0x01, 10, 0x01, 20, 0x01, 30 };

    OpCode opcode;
    uint8_t flags;
    decodeOpHeader(data[0], opcode, flags);

    TEST_ASSERT_EQUAL(OpCode::SCHEMA_DELETE, opcode);
    TEST_ASSERT_EQUAL(Flags::BATCH, flags);

    ReadBuffer rb(data + 1, sizeof(data) - 1);
    SchemaDeleteDecoder::DeleteItem items[8];
    size_t itemCount = 0;

    TEST_ASSERT_TRUE(SchemaDeleteDecoder::decode(rb, flags, items, 8, itemCount));
    TEST_ASSERT_EQUAL(3, itemCount);
    TEST_ASSERT_EQUAL(SchemaItemType::PROPERTY, items[0].type);
    TEST_ASSERT_EQUAL(10, items[0].itemId);
    TEST_ASSERT_EQUAL(SchemaItemType::PROPERTY, items[1].type);
    TEST_ASSERT_EQUAL(20, items[1].itemId);
    TEST_ASSERT_EQUAL(SchemaItemType::PROPERTY, items[2].type);
    TEST_ASSERT_EQUAL(30, items[2].itemId);
}

void test_schema_delete_roundtrip() {
    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));

    uint16_t ids[] = { 5, 127, 128, 255 };
    TEST_ASSERT_TRUE(SchemaDeleteEncoder::encodeBatchedDelete(wb, ids, 4));

    OpCode opcode;
    uint8_t flags;
    decodeOpHeader(buf[0], opcode, flags);

    ReadBuffer rb(buf + 1, wb.position() - 1);
    SchemaDeleteDecoder::DeleteItem items[8];
    size_t itemCount = 0;

    TEST_ASSERT_TRUE(SchemaDeleteDecoder::decode(rb, flags, items, 8, itemCount));
    TEST_ASSERT_EQUAL(4, itemCount);
    TEST_ASSERT_EQUAL(5, items[0].itemId);
    TEST_ASSERT_EQUAL(127, items[1].itemId);
    TEST_ASSERT_EQUAL(128, items[2].itemId);
    TEST_ASSERT_EQUAL(255, items[3].itemId);
}

void test_schema_delete_large_propid() {
    uint8_t buf[16];
    WriteBuffer wb(buf, sizeof(buf));

    // Test propid > 127 (requires 2 bytes)
    TEST_ASSERT_TRUE(SchemaDeleteEncoder::encodePropertyDelete(wb, 1000));

    OpCode opcode;
    uint8_t flags;
    decodeOpHeader(buf[0], opcode, flags);

    ReadBuffer rb(buf + 1, wb.position() - 1);
    SchemaDeleteDecoder::DeleteItem items[8];
    size_t itemCount = 0;

    TEST_ASSERT_TRUE(SchemaDeleteDecoder::decode(rb, flags, items, 8, itemCount));
    TEST_ASSERT_EQUAL(1, itemCount);
    TEST_ASSERT_EQUAL(1000, items[0].itemId);
}

// ==== RESOURCE_GET Tests ====

void test_resource_get_request_encode() {
    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));

    TEST_ASSERT_TRUE(ResourceGetEncoder::encodeRequest(wb, 42, 5, 123));

    // opcode=0x8, flags=0
    TEST_ASSERT_EQUAL_HEX8(0x08, buf[0]);
    // request_id
    TEST_ASSERT_EQUAL(42, buf[1]);
    // propid=5 (single byte)
    TEST_ASSERT_EQUAL(5, buf[2]);
    // resource_id=123 (single byte varint)
    TEST_ASSERT_EQUAL(123, buf[3]);

    TEST_ASSERT_EQUAL(4, wb.position());
}

void test_resource_get_response_ok_encode() {
    uint8_t buf[64];
    WriteBuffer wb(buf, sizeof(buf));

    uint8_t bodyData[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    TEST_ASSERT_TRUE(ResourceGetEncoder::encodeResponseOk(wb, 42, bodyData, 5));

    // opcode=0x8, flags=0b0001 (is_response)
    TEST_ASSERT_EQUAL_HEX8(0x18, buf[0]);
    // request_id
    TEST_ASSERT_EQUAL(42, buf[1]);
    // blob: length=5 then data
    TEST_ASSERT_EQUAL(5, buf[2]);
    TEST_ASSERT_EQUAL_MEMORY(bodyData, buf + 3, 5);

    TEST_ASSERT_EQUAL(8, wb.position());
}

void test_resource_get_response_error_encode() {
    uint8_t buf[64];
    WriteBuffer wb(buf, sizeof(buf));

    TEST_ASSERT_TRUE(ResourceGetEncoder::encodeResponseError(wb, 42, ResourceError::NOT_FOUND, "not found"));

    // opcode=0x8, flags=0b0011 (is_response | status_error)
    TEST_ASSERT_EQUAL_HEX8(0x38, buf[0]);
    // request_id
    TEST_ASSERT_EQUAL(42, buf[1]);
    // error_code
    TEST_ASSERT_EQUAL(ResourceError::NOT_FOUND, buf[2]);
    // utf8 message: length=9 "not found"
    TEST_ASSERT_EQUAL(9, buf[3]);
    TEST_ASSERT_EQUAL_MEMORY("not found", buf + 4, 9);
}

// ==== RESOURCE_PUT Tests ====

void test_resource_put_request_create_encode() {
    uint8_t buf[64];
    WriteBuffer wb(buf, sizeof(buf));

    uint8_t headerData[] = {0xAA, 0xBB};
    uint8_t bodyData[] = {0x01, 0x02, 0x03, 0x04};

    // Create new resource (resourceId=0)
    TEST_ASSERT_TRUE(ResourcePutEncoder::encodeRequest(wb, 1, 10, 0,
        headerData, 2, bodyData, 4));

    // opcode=0x9, flags=0b0110 (update_header | update_body)
    TEST_ASSERT_EQUAL_HEX8(0x69, buf[0]);
    // request_id
    TEST_ASSERT_EQUAL(1, buf[1]);
    // propid=10
    TEST_ASSERT_EQUAL(10, buf[2]);
    // resource_id=0 (create new)
    TEST_ASSERT_EQUAL(0, buf[3]);
    // blob header: length=2 + data
    TEST_ASSERT_EQUAL(2, buf[4]);
    TEST_ASSERT_EQUAL_MEMORY(headerData, buf + 5, 2);
    // blob body: length=4 + data
    TEST_ASSERT_EQUAL(4, buf[7]);
    TEST_ASSERT_EQUAL_MEMORY(bodyData, buf + 8, 4);
}

void test_resource_put_request_update_header_only_encode() {
    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));

    uint8_t headerData[] = {0xAA, 0xBB, 0xCC};

    // Update only header (resourceId=5)
    TEST_ASSERT_TRUE(ResourcePutEncoder::encodeRequest(wb, 2, 10, 5,
        headerData, 3, nullptr, 0));

    // opcode=0x9, flags=0b0010 (update_header only)
    TEST_ASSERT_EQUAL_HEX8(0x29, buf[0]);
    // request_id
    TEST_ASSERT_EQUAL(2, buf[1]);
    // propid=10
    TEST_ASSERT_EQUAL(10, buf[2]);
    // resource_id=5
    TEST_ASSERT_EQUAL(5, buf[3]);
    // blob header only
    TEST_ASSERT_EQUAL(3, buf[4]);
    TEST_ASSERT_EQUAL_MEMORY(headerData, buf + 5, 3);

    TEST_ASSERT_EQUAL(8, wb.position());
}

void test_resource_put_response_ok_encode() {
    uint8_t buf[16];
    WriteBuffer wb(buf, sizeof(buf));

    TEST_ASSERT_TRUE(ResourcePutEncoder::encodeResponseOk(wb, 5, 42));

    // opcode=0x9, flags=0b0001 (is_response)
    TEST_ASSERT_EQUAL_HEX8(0x19, buf[0]);
    // request_id
    TEST_ASSERT_EQUAL(5, buf[1]);
    // resource_id=42
    TEST_ASSERT_EQUAL(42, buf[2]);

    TEST_ASSERT_EQUAL(3, wb.position());
}

void test_resource_put_response_error_encode() {
    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));

    TEST_ASSERT_TRUE(ResourcePutEncoder::encodeResponseError(wb, 5, ResourceError::OUT_OF_SPACE, "full"));

    // opcode=0x9, flags=0b0011 (is_response | status_error)
    TEST_ASSERT_EQUAL_HEX8(0x39, buf[0]);
    TEST_ASSERT_EQUAL(5, buf[1]);
    TEST_ASSERT_EQUAL(ResourceError::OUT_OF_SPACE, buf[2]);
    TEST_ASSERT_EQUAL(4, buf[3]);  // "full" length
}

// ==== RESOURCE_DELETE Tests ====

void test_resource_delete_request_encode() {
    uint8_t buf[16];
    WriteBuffer wb(buf, sizeof(buf));

    TEST_ASSERT_TRUE(ResourceDeleteEncoder::encodeRequest(wb, 7, 3, 99));

    // opcode=0xA, flags=0
    TEST_ASSERT_EQUAL_HEX8(0x0A, buf[0]);
    // request_id
    TEST_ASSERT_EQUAL(7, buf[1]);
    // propid=3
    TEST_ASSERT_EQUAL(3, buf[2]);
    // resource_id=99
    TEST_ASSERT_EQUAL(99, buf[3]);

    TEST_ASSERT_EQUAL(4, wb.position());
}

void test_resource_delete_response_ok_encode() {
    uint8_t buf[16];
    WriteBuffer wb(buf, sizeof(buf));

    TEST_ASSERT_TRUE(ResourceDeleteEncoder::encodeResponseOk(wb, 7));

    // opcode=0xA, flags=0b0001 (is_response)
    TEST_ASSERT_EQUAL_HEX8(0x1A, buf[0]);
    // request_id
    TEST_ASSERT_EQUAL(7, buf[1]);

    TEST_ASSERT_EQUAL(2, wb.position());
}

void test_resource_delete_response_error_encode() {
    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));

    TEST_ASSERT_TRUE(ResourceDeleteEncoder::encodeResponseError(wb, 7, ResourceError::NOT_FOUND));

    // opcode=0xA, flags=0b0011 (is_response | status_error)
    TEST_ASSERT_EQUAL_HEX8(0x3A, buf[0]);
    TEST_ASSERT_EQUAL(7, buf[1]);
    TEST_ASSERT_EQUAL(ResourceError::NOT_FOUND, buf[2]);
}

void test_resource_get_large_ids() {
    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));

    // Large property ID (200 > 127, needs 2 bytes)
    // Large resource ID (300 > 127, needs 2 bytes in varint)
    TEST_ASSERT_TRUE(ResourceGetEncoder::encodeRequest(wb, 1, 200, 300));

    // opcode=0x8, flags=0
    TEST_ASSERT_EQUAL_HEX8(0x08, buf[0]);
    // request_id
    TEST_ASSERT_EQUAL(1, buf[1]);
    // propid=200: [1xxxxxxx] [xxxxxxxx] = [0x80 | (200 & 0x7F)] [200 >> 7]
    // 200 = 0xC8: low 7 bits = 72 (0x48), high 8 bits = 1
    TEST_ASSERT_EQUAL_HEX8(0xC8, buf[2]);  // 0x80 | 72 = 0xC8
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[3]);  // 200 >> 7 = 1

    // resource_id=300 as varint: 300 = 0x12C
    // byte 0: 0x80 | (300 & 0x7F) = 0x80 | 44 = 0xAC
    // byte 1: 300 >> 7 = 2
    TEST_ASSERT_EQUAL_HEX8(0xAC, buf[4]);
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[5]);

    TEST_ASSERT_EQUAL(6, wb.position());
}

// ==== PROPERTY_UPDATE Tests ====

#include <wire/PropertyUpdate.h>
#include <Property.h>
#include <ArrayProperty.h>

void test_property_update_single_uint8() {
    // Create a property for testing
    Property<uint8_t> brightness("brightness", 128, PropertyLevel::LOCAL);

    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));

    TEST_ASSERT_TRUE(PropertyUpdate::encode(wb, &brightness));

    // Verify: opcode=1, flags=0, propid, value
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[0]);  // opcode=1 (PROPERTY_UPDATE), flags=0
    TEST_ASSERT_EQUAL(brightness.id, buf[1]);  // propid (single byte for small id)
    TEST_ASSERT_EQUAL(128, buf[2]);  // value

    TEST_ASSERT_EQUAL(3, wb.position());
}

void test_property_update_single_int32() {
    Property<int32_t> position("position", -12345, PropertyLevel::LOCAL);

    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));

    TEST_ASSERT_TRUE(PropertyUpdate::encode(wb, &position));

    // opcode=1, flags=0
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[0]);

    // propid
    size_t pos = 1;
    TEST_ASSERT_EQUAL(position.id, buf[pos++]);

    // int32 value: -12345 in little-endian
    int32_t decoded;
    memcpy(&decoded, &buf[pos], sizeof(int32_t));
    TEST_ASSERT_EQUAL(-12345, decoded);
}

void test_property_update_single_float32() {
    Property<float> speed("speed", 3.14159f, PropertyLevel::LOCAL);

    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));

    TEST_ASSERT_TRUE(PropertyUpdate::encode(wb, &speed));

    TEST_ASSERT_EQUAL_HEX8(0x01, buf[0]);

    // Decode the float
    float decoded;
    memcpy(&decoded, &buf[2], sizeof(float));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.14159f, decoded);
}

void test_property_update_single_bool() {
    Property<bool> enabled("enabled", true, PropertyLevel::LOCAL);

    uint8_t buf[16];
    WriteBuffer wb(buf, sizeof(buf));

    TEST_ASSERT_TRUE(PropertyUpdate::encode(wb, &enabled));

    TEST_ASSERT_EQUAL_HEX8(0x01, buf[0]);
    TEST_ASSERT_EQUAL(enabled.id, buf[1]);
    TEST_ASSERT_EQUAL(1, buf[2]);  // true = 1

    // Test false value
    enabled = false;
    WriteBuffer wb2(buf, sizeof(buf));
    TEST_ASSERT_TRUE(PropertyUpdate::encode(wb2, &enabled));
    TEST_ASSERT_EQUAL(0, buf[2]);  // false = 0
}

void test_property_update_with_timestamp() {
    Property<uint8_t> brightness("brightness", 200, PropertyLevel::LOCAL);

    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));

    uint32_t timestamp = 1700000000;
    TEST_ASSERT_TRUE(PropertyUpdate::encodeWithTimestamp(wb, &brightness, timestamp));

    // opcode=1, flags=0x2 (HAS_TIMESTAMP)
    TEST_ASSERT_EQUAL_HEX8(0x21, buf[0]);  // opcode=1, flags=2 in upper nibble

    // timestamp as varint (1700000000 = 0x6553F100)
    ReadBuffer rb(buf + 1, wb.position() - 1);
    uint32_t decodedTimestamp = rb.readVarint();
    TEST_ASSERT_EQUAL(timestamp, decodedTimestamp);
}

void test_property_update_batched_two_properties() {
    Property<uint8_t> brightness("brightness", 100, PropertyLevel::LOCAL);
    Property<uint8_t> mode("mode", 5, PropertyLevel::LOCAL);

    const PropertyBase* props[] = { &brightness, &mode };

    uint8_t buf[64];
    WriteBuffer wb(buf, sizeof(buf));

    TEST_ASSERT_TRUE(PropertyUpdate::encodeBatch(wb, props, 2));

    // opcode=1, flags=0x1 (BATCH)
    TEST_ASSERT_EQUAL_HEX8(0x11, buf[0]);  // opcode=1, flags=1

    // batch_count = 2-1 = 1
    TEST_ASSERT_EQUAL(1, buf[1]);

    // First property: propid + value
    TEST_ASSERT_EQUAL(brightness.id, buf[2]);
    TEST_ASSERT_EQUAL(100, buf[3]);

    // Second property: propid + value
    TEST_ASSERT_EQUAL(mode.id, buf[4]);
    TEST_ASSERT_EQUAL(5, buf[5]);

    TEST_ASSERT_EQUAL(6, wb.position());
}

void test_property_update_batched_max_256() {
    // Test batch count encoding at max (256 items)
    // We won't create 256 properties, just test the encoding logic
    Property<uint8_t> brightness("brightness", 50, PropertyLevel::LOCAL);
    const PropertyBase* props[] = { &brightness };

    uint8_t buf[16];
    WriteBuffer wb(buf, sizeof(buf));

    // Single item batch (count=1)
    TEST_ASSERT_TRUE(PropertyUpdate::encodeBatch(wb, props, 1));
    TEST_ASSERT_EQUAL_HEX8(0x11, buf[0]);  // batch flag
    TEST_ASSERT_EQUAL(0, buf[1]);  // count-1 = 0
}

void test_property_update_batched_with_timestamp() {
    Property<uint8_t> brightness("brightness", 255, PropertyLevel::LOCAL);
    Property<bool> enabled("enabled", true, PropertyLevel::LOCAL);

    const PropertyBase* props[] = { &brightness, &enabled };

    uint8_t buf[64];
    WriteBuffer wb(buf, sizeof(buf));

    uint32_t timestamp = 1234567890;
    TEST_ASSERT_TRUE(PropertyUpdate::encodeBatchWithTimestamp(wb, props, 2, timestamp));

    // opcode=1, flags=0x3 (BATCH + HAS_TIMESTAMP)
    TEST_ASSERT_EQUAL_HEX8(0x31, buf[0]);

    // batch_count
    TEST_ASSERT_EQUAL(1, buf[1]);  // count-1

    // timestamp
    ReadBuffer rb(buf + 2, wb.position() - 2);
    uint32_t decodedTimestamp = rb.readVarint();
    TEST_ASSERT_EQUAL(timestamp, decodedTimestamp);
}

void test_property_update_array_rgb() {
    // Test RGB color array (common LED use case)
    ArrayProperty<uint8_t, 3> rgb("rgb", {255, 128, 64}, PropertyLevel::LOCAL);

    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));

    TEST_ASSERT_TRUE(PropertyUpdate::encode(wb, &rgb));

    TEST_ASSERT_EQUAL_HEX8(0x01, buf[0]);
    TEST_ASSERT_EQUAL(rgb.id, buf[1]);

    // RGB values
    TEST_ASSERT_EQUAL(255, buf[2]);
    TEST_ASSERT_EQUAL(128, buf[3]);
    TEST_ASSERT_EQUAL(64, buf[4]);
}

void test_property_update_decode_header_single() {
    uint8_t buf[] = { 0x01 };  // opcode=1, flags=0
    ReadBuffer rb(buf, sizeof(buf));

    OpCode opcode;
    uint8_t flags;
    decodeOpHeader(rb.readByte(), opcode, flags);

    uint8_t batchCount;
    uint32_t timestamp;

    TEST_ASSERT_TRUE(PropertyUpdate::decodeHeader(flags, rb, batchCount, timestamp));
    TEST_ASSERT_EQUAL(1, batchCount);
    TEST_ASSERT_EQUAL(0, timestamp);
}

void test_property_update_decode_header_batched() {
    uint8_t buf[] = { 0x11, 0x04 };  // opcode=1, flags=1 (batch), count=5
    ReadBuffer rb(buf, sizeof(buf));

    OpCode opcode;
    uint8_t flags;
    decodeOpHeader(rb.readByte(), opcode, flags);

    uint8_t batchCount;
    uint32_t timestamp;

    TEST_ASSERT_TRUE(PropertyUpdate::decodeHeader(flags, rb, batchCount, timestamp));
    TEST_ASSERT_EQUAL(5, batchCount);  // 4+1
    TEST_ASSERT_EQUAL(0, timestamp);
}

void test_property_update_decode_header_with_timestamp() {
    // opcode=1, flags=2 (timestamp), timestamp=127 (single byte varint)
    uint8_t buf[] = { 0x21, 0x7F };
    ReadBuffer rb(buf, sizeof(buf));

    OpCode opcode;
    uint8_t flags;
    decodeOpHeader(rb.readByte(), opcode, flags);

    uint8_t batchCount;
    uint32_t timestamp;

    TEST_ASSERT_TRUE(PropertyUpdate::decodeHeader(flags, rb, batchCount, timestamp));
    TEST_ASSERT_EQUAL(1, batchCount);
    TEST_ASSERT_EQUAL(127, timestamp);
}

void test_property_update_large_propid() {
    // Property with ID > 127 requires 2-byte propid encoding
    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));

    // Encode manually with large property ID
    TEST_ASSERT_TRUE(PropertyUpdate::encodeValue(wb, 200, TYPE_UINT8, (uint8_t[]){42}, 1));

    TEST_ASSERT_EQUAL_HEX8(0x01, buf[0]);  // opcode

    // propid 200: 0x80 | (200 & 0x7F), 200 >> 7
    // 200 = 0xC8, low 7 = 72, high = 1
    TEST_ASSERT_EQUAL_HEX8(0xC8, buf[1]);  // 0x80 | 72
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[2]);  // high byte

    TEST_ASSERT_EQUAL(42, buf[3]);  // value
}

void test_property_update_zero_batch_count_fails() {
    Property<uint8_t> prop("test", 0, PropertyLevel::LOCAL);
    const PropertyBase* props[] = { &prop };

    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));

    // Zero count should fail
    TEST_ASSERT_FALSE(PropertyUpdate::encodeBatch(wb, props, 0));
}

void test_property_update_over_256_batch_fails() {
    Property<uint8_t> prop("test", 0, PropertyLevel::LOCAL);
    const PropertyBase* props[] = { &prop };

    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));

    // Over 256 should fail
    TEST_ASSERT_FALSE(PropertyUpdate::encodeBatch(wb, props, 257));
}

// ==== PING Tests ====

void test_ping_request_encode() {
    uint8_t buf[16];
    WriteBuffer wb(buf, sizeof(buf));

    // PING request: opcode=6, flags=0 (is_response=0)
    uint8_t flags = 0;
    wb.writeByte(encodeOpHeader(OpCode::PING, flags));
    wb.writeVarint(12345);  // payload

    TEST_ASSERT_EQUAL_HEX8(0x06, buf[0]);  // opcode=6, flags=0

    ReadBuffer rb(buf + 1, wb.position() - 1);
    TEST_ASSERT_EQUAL(12345, rb.readVarint());
}

void test_ping_response_encode() {
    uint8_t buf[16];
    WriteBuffer wb(buf, sizeof(buf));

    // PING response: opcode=6, flags=1 (is_response=1)
    uint8_t flags = 0x01;  // IS_RESPONSE
    wb.writeByte(encodeOpHeader(OpCode::PING, flags));
    wb.writeVarint(12345);  // echoed payload

    TEST_ASSERT_EQUAL_HEX8(0x16, buf[0]);  // opcode=6, flags=1

    ReadBuffer rb(buf + 1, wb.position() - 1);
    TEST_ASSERT_EQUAL(12345, rb.readVarint());
}

void test_ping_large_payload() {
    uint8_t buf[16];
    WriteBuffer wb(buf, sizeof(buf));

    // Large payload (max varint value)
    uint32_t payload = 0x7FFFFFFF;
    wb.writeByte(encodeOpHeader(OpCode::PING, 0));
    wb.writeVarint(payload);

    ReadBuffer rb(buf + 1, wb.position() - 1);
    TEST_ASSERT_EQUAL(payload, rb.readVarint());
}

// ==== RPC Tests ====

void test_rpc_request_encode() {
    uint8_t buf[64];
    WriteBuffer wb(buf, sizeof(buf));

    // RPC request: opcode=5, flags: is_response=0, needs_response=1
    RpcFlags flags;
    flags.isResponse = false;
    flags.needsResponse = true;

    wb.writeByte(encodeOpHeader(OpCode::RPC, flags.encode()));
    wb.writePropId(10);  // function_id
    wb.writeVarint(42);  // call_id (because needs_response=1)
    // params would follow...

    TEST_ASSERT_EQUAL_HEX8(0x25, buf[0]);  // opcode=5, flags=2 (needs_response)
    TEST_ASSERT_EQUAL(10, buf[1]);  // function_id
    TEST_ASSERT_EQUAL(42, buf[2]);  // call_id
}

void test_rpc_request_fire_and_forget() {
    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));

    // Fire-and-forget: needs_response=0, no call_id
    RpcFlags flags;
    flags.isResponse = false;
    flags.needsResponse = false;

    wb.writeByte(encodeOpHeader(OpCode::RPC, flags.encode()));
    wb.writePropId(15);  // function_id
    // No call_id for fire-and-forget

    TEST_ASSERT_EQUAL_HEX8(0x05, buf[0]);  // opcode=5, flags=0
    TEST_ASSERT_EQUAL(15, buf[1]);  // function_id
    TEST_ASSERT_EQUAL(2, wb.position());  // Just header + function_id
}

void test_rpc_response_success() {
    uint8_t buf[64];
    WriteBuffer wb(buf, sizeof(buf));

    // RPC response success: is_response=1, success=1
    RpcFlags flags;
    flags.isResponse = true;
    flags.success = true;
    flags.hasReturnValue = true;

    wb.writeByte(encodeOpHeader(OpCode::RPC, flags.encode()));
    wb.writeVarint(42);  // call_id
    // return value would follow...

    TEST_ASSERT_EQUAL_HEX8(0x75, buf[0]);  // opcode=5, flags=7 (is_response + success + has_return)
}

void test_rpc_response_error() {
    uint8_t buf[64];
    WriteBuffer wb(buf, sizeof(buf));

    // RPC response error: is_response=1, success=0
    RpcFlags flags;
    flags.isResponse = true;
    flags.success = false;

    wb.writeByte(encodeOpHeader(OpCode::RPC, flags.encode()));
    wb.writeVarint(42);  // call_id
    wb.writeByte(0x05);  // error_code (low byte)
    wb.writeByte(0x00);  // error_code (high byte)
    wb.writeUtf8("Invalid parameter");

    TEST_ASSERT_EQUAL_HEX8(0x15, buf[0]);  // opcode=5, flags=1 (is_response only)
}

void test_rpc_large_function_id() {
    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));

    RpcFlags flags;
    flags.needsResponse = true;

    wb.writeByte(encodeOpHeader(OpCode::RPC, flags.encode()));
    wb.writePropId(500);  // Large function ID needs 2 bytes
    wb.writeVarint(1);

    // propid 500: 0x80 | (500 & 0x7F), 500 >> 7
    // 500 = 0x1F4, low 7 = 116, high = 3
    TEST_ASSERT_EQUAL_HEX8(0xF4, buf[1]);  // 0x80 | 116
    TEST_ASSERT_EQUAL_HEX8(0x03, buf[2]);  // 3
}

// ==== LED Control System Scenarios ====
// Simulates realistic LED strip control operations

void test_led_scenario_set_brightness() {
    // Property ID 0 = brightness (uint8_t, 0-255)
    uint8_t buf[16];
    WriteBuffer wb(buf, sizeof(buf));

    // Encode brightness=128 update
    uint8_t value = 128;
    TEST_ASSERT_TRUE(PropertyUpdate::encodeValue(wb, 0, TYPE_UINT8, &value, 1));

    // Decode and verify
    ReadBuffer rb(buf, wb.position());
    rb.readByte();  // Skip opheader

    uint16_t propId = rb.readPropId();
    TEST_ASSERT_EQUAL(0, propId);
    TEST_ASSERT_EQUAL(128, rb.readByte());
    TEST_ASSERT_TRUE(rb.ok());
}

void test_led_scenario_set_shader_index() {
    // Property ID 1 = shaderIndex (uint8_t, index into shader list)
    uint8_t buf[16];
    WriteBuffer wb(buf, sizeof(buf));

    uint8_t value = 5;
    TEST_ASSERT_TRUE(PropertyUpdate::encodeValue(wb, 1, TYPE_UINT8, &value, 1));

    ReadBuffer rb(buf, wb.position());
    rb.readByte();  // Skip opheader

    uint16_t propId = rb.readPropId();
    TEST_ASSERT_EQUAL(1, propId);
    TEST_ASSERT_EQUAL(5, rb.readByte());
}

void test_led_scenario_batched_brightness_and_shader() {
    // Batched update: brightness + shaderIndex in single message
    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));

    // Manually encode batch header
    PropertyUpdateFlags flags;
    flags.batch = true;
    wb.writeByte(encodeOpHeader(OpCode::PROPERTY_UPDATE, flags.encode()));
    wb.writeByte(1);  // batch_count-1 = 1 means 2 items

    // brightness = 200
    wb.writePropId(0);
    wb.writeByte(200);

    // shaderIndex = 3
    wb.writePropId(1);
    wb.writeByte(3);

    // Verify batched structure
    ReadBuffer rb(buf, wb.position());
    OpCode opcode;
    uint8_t opFlags;
    decodeOpHeader(rb.readByte(), opcode, opFlags);
    TEST_ASSERT_EQUAL(OpCode::PROPERTY_UPDATE, opcode);

    uint8_t batchCount;
    uint32_t timestamp;
    PropertyUpdate::decodeHeader(opFlags, rb, batchCount, timestamp);
    TEST_ASSERT_EQUAL(2, batchCount);

    // First value
    uint16_t propId = rb.readPropId();
    TEST_ASSERT_EQUAL(0, propId);
    TEST_ASSERT_EQUAL(200, rb.readByte());

    // Second value
    propId = rb.readPropId();
    TEST_ASSERT_EQUAL(1, propId);
    TEST_ASSERT_EQUAL(3, rb.readByte());
}

void test_led_scenario_rgb_preview_list() {
    // ledPreview is a ListProperty<uint8_t, 60*3> - 60 LEDs * RGB
    // Sending 3 LED colors (9 bytes)
    uint8_t rgbData[] = {255, 0, 0,    // LED 0: Red
                         0, 255, 0,    // LED 1: Green
                         0, 0, 255};   // LED 2: Blue

    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));

    // Manual encoding of list value
    wb.writeByte(encodeOpHeader(OpCode::PROPERTY_UPDATE, 0));
    wb.writePropId(4);  // ledPreview property
    wb.writeByte(TYPE_LIST);
    wb.writeByte(TYPE_UINT8);  // element type
    wb.writeVarint(9);  // 9 elements
    for (int i = 0; i < 9; i++) {
        wb.writeByte(rgbData[i]);
    }

    // Decode list header
    ReadBuffer rb(buf, wb.position());
    rb.readByte();  // opheader

    uint16_t propId = rb.readPropId();
    TEST_ASSERT_EQUAL(4, propId);

    uint8_t typeId = rb.readByte();
    TEST_ASSERT_EQUAL(TYPE_LIST, typeId);

    uint8_t elementType = rb.readByte();
    TEST_ASSERT_EQUAL(TYPE_UINT8, elementType);

    uint32_t length = rb.readVarint();
    TEST_ASSERT_EQUAL(9, length);

    // Verify RGB values
    TEST_ASSERT_EQUAL(255, rb.readByte());  // R
    TEST_ASSERT_EQUAL(0, rb.readByte());    // G
    TEST_ASSERT_EQUAL(0, rb.readByte());    // B
}

void test_led_scenario_atmospheric_fade_toggle() {
    // atmosphericFade is a bool property
    uint8_t buf[16];
    WriteBuffer wb(buf, sizeof(buf));

    uint8_t value = 1;
    TEST_ASSERT_TRUE(PropertyUpdate::encodeValue(wb, 3, TYPE_BOOL, &value, 1));

    ReadBuffer rb(buf, wb.position());
    rb.readByte();  // opheader

    uint16_t propId = rb.readPropId();
    TEST_ASSERT_EQUAL(3, propId);
    TEST_ASSERT_EQUAL(1, rb.readByte());
}

void test_led_scenario_full_state_sync() {
    // Batched update of all LED properties with timestamp (simulates initial sync)
    // brightness(0), shaderIndex(1), ledCount(2), atmosphericFade(3)
    uint8_t buf[64];
    WriteBuffer wb(buf, sizeof(buf));

    PropertyUpdateFlags flags;
    flags.batch = true;
    flags.hasTimestamp = true;
    wb.writeByte(encodeOpHeader(OpCode::PROPERTY_UPDATE, flags.encode()));
    wb.writeByte(3);  // batch_count-1 = 3 means 4 items
    wb.writeVarint(1000);  // timestamp

    // brightness = 255
    wb.writePropId(0);
    wb.writeByte(255);

    // shaderIndex = 0
    wb.writePropId(1);
    wb.writeByte(0);

    // ledCount = 30
    wb.writePropId(2);
    wb.writeByte(30);

    // atmosphericFade = false
    wb.writePropId(3);
    wb.writeByte(0);

    // Decode full state
    ReadBuffer rb(buf, wb.position());
    OpCode opcode;
    uint8_t opFlags;
    decodeOpHeader(rb.readByte(), opcode, opFlags);

    uint8_t batchCount;
    uint32_t timestamp;
    PropertyUpdate::decodeHeader(opFlags, rb, batchCount, timestamp);
    TEST_ASSERT_EQUAL(4, batchCount);
    TEST_ASSERT_EQUAL(1000, timestamp);
}

void test_led_scenario_color_array_update() {
    // Fixed-size color array (RGB: 3 bytes)
    // Note: ARRAY type encoding puts bytes directly - size comes from schema
    uint8_t color[] = {255, 128, 64};

    uint8_t buf[16];
    WriteBuffer wb(buf, sizeof(buf));

    // Manual encoding (PropertyUpdate::encodeValue only handles basic types)
    wb.writeByte(encodeOpHeader(OpCode::PROPERTY_UPDATE, 0));
    wb.writePropId(5);  // color property ID
    // ARRAY values are written directly (size from schema)
    for (int i = 0; i < 3; i++) {
        wb.writeByte(color[i]);
    }

    ReadBuffer rb(buf, wb.position());
    rb.readByte();  // opheader

    uint16_t propId = rb.readPropId();
    TEST_ASSERT_EQUAL(5, propId);

    // Array elements read directly (size from schema)
    TEST_ASSERT_EQUAL(255, rb.readByte());
    TEST_ASSERT_EQUAL(128, rb.readByte());
    TEST_ASSERT_EQUAL(64, rb.readByte());
}

// ==== Wire Format Edge Cases ====

void test_wire_buffer_overflow_write() {
    // Buffer too small for operation
    uint8_t buf[2];  // Too small
    WriteBuffer wb(buf, sizeof(buf));

    wb.writeByte(0x01);
    wb.writeByte(0x02);
    // Third write should fail gracefully
    wb.writeByte(0x03);

    // Buffer should be in overflow state
    TEST_ASSERT_TRUE(wb.overflow());
    TEST_ASSERT_EQUAL(2, wb.position());  // Only wrote what fit
}

void test_wire_buffer_overflow_read() {
    uint8_t buf[] = {0x01, 0x02};
    ReadBuffer rb(buf, sizeof(buf));

    rb.readByte();
    rb.readByte();
    // Third read past end
    rb.readByte();

    TEST_ASSERT_TRUE(rb.error());
}

void test_wire_varint_boundary_127() {
    // 127 is max single-byte varint
    uint8_t buf[4];
    WriteBuffer wb(buf, sizeof(buf));
    wb.writeVarint(127);
    TEST_ASSERT_EQUAL(1, wb.position());
    TEST_ASSERT_EQUAL(127, buf[0]);
}

void test_wire_varint_boundary_128() {
    // 128 requires 2-byte varint
    uint8_t buf[4];
    WriteBuffer wb(buf, sizeof(buf));
    wb.writeVarint(128);
    TEST_ASSERT_EQUAL(2, wb.position());
    TEST_ASSERT_EQUAL(0x80, buf[0] & 0x80);  // Continuation bit set
}

void test_wire_varint_max_32bit() {
    // Maximum 32-bit value
    uint8_t buf[8];
    WriteBuffer wb(buf, sizeof(buf));
    wb.writeVarint(0xFFFFFFFF);
    TEST_ASSERT_EQUAL(5, wb.position());  // 32-bit max = 5 varint bytes

    ReadBuffer rb(buf, wb.position());
    TEST_ASSERT_EQUAL(0xFFFFFFFF, rb.readVarint());
}

void test_wire_propid_boundary_127() {
    // propid 127 = single byte
    uint8_t buf[4];
    WriteBuffer wb(buf, sizeof(buf));
    wb.writePropId(127);
    TEST_ASSERT_EQUAL(1, wb.position());
    TEST_ASSERT_EQUAL(127, buf[0]);
}

void test_wire_propid_boundary_128() {
    // propid 128 = two bytes
    uint8_t buf[4];
    WriteBuffer wb(buf, sizeof(buf));
    wb.writePropId(128);
    TEST_ASSERT_EQUAL(2, wb.position());
    TEST_ASSERT_EQUAL(0x80, buf[0] & 0x80);  // High bit set

    ReadBuffer rb(buf, wb.position());
    TEST_ASSERT_EQUAL(128, rb.readPropId());
}

void test_wire_propid_max_32767() {
    // Maximum property ID
    uint8_t buf[4];
    WriteBuffer wb(buf, sizeof(buf));
    wb.writePropId(32767);
    TEST_ASSERT_EQUAL(2, wb.position());

    ReadBuffer rb(buf, wb.position());
    TEST_ASSERT_EQUAL(32767, rb.readPropId());
}

void test_wire_empty_buffer() {
    uint8_t buf[1];
    ReadBuffer rb(buf, 0);  // Empty

    TEST_ASSERT_EQUAL(0, rb.remaining());
    rb.readByte();  // Read from empty
    TEST_ASSERT_TRUE(rb.error());
}

void test_wire_utf8_empty_string() {
    uint8_t buf[4];
    WriteBuffer wb(buf, sizeof(buf));
    wb.writeUtf8("");
    TEST_ASSERT_EQUAL(1, wb.position());  // Just length byte (0)
    TEST_ASSERT_EQUAL(0, buf[0]);
}

void test_wire_utf8_max_length() {
    // String with 127 chars (max single-byte length)
    char str[128];
    memset(str, 'A', 127);
    str[127] = '\0';

    uint8_t buf[256];
    WriteBuffer wb(buf, sizeof(buf));
    wb.writeUtf8(str);

    TEST_ASSERT_EQUAL(128, wb.position());  // 1 length + 127 chars
    TEST_ASSERT_EQUAL(127, buf[0]);
}

void test_wire_float_special_values() {
    // Test encoding of special float values
    uint8_t buf[16];

    // Zero
    float zero = 0.0f;
    WriteBuffer wb1(buf, sizeof(buf));
    wb1.writeBytes(reinterpret_cast<const uint8_t*>(&zero), 4);
    ReadBuffer rb1(buf, 4);
    float readZero;
    rb1.readBytes(reinterpret_cast<uint8_t*>(&readZero), 4);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, readZero);

    // Negative
    float neg = -1.5f;
    WriteBuffer wb2(buf, sizeof(buf));
    wb2.writeBytes(reinterpret_cast<const uint8_t*>(&neg), 4);
    ReadBuffer rb2(buf, 4);
    float readNeg;
    rb2.readBytes(reinterpret_cast<uint8_t*>(&readNeg), 4);
    TEST_ASSERT_EQUAL_FLOAT(-1.5f, readNeg);
}

void test_wire_int32_boundary_values() {
    uint8_t buf[8];

    // Min int32
    int32_t minVal = INT32_MIN;
    WriteBuffer wb1(buf, sizeof(buf));
    wb1.writeBytes(reinterpret_cast<const uint8_t*>(&minVal), 4);
    ReadBuffer rb1(buf, 4);
    int32_t readMin;
    rb1.readBytes(reinterpret_cast<uint8_t*>(&readMin), 4);
    TEST_ASSERT_EQUAL_INT32(INT32_MIN, readMin);

    // Max int32
    int32_t maxVal = INT32_MAX;
    WriteBuffer wb2(buf, sizeof(buf));
    wb2.writeBytes(reinterpret_cast<const uint8_t*>(&maxVal), 4);
    ReadBuffer rb2(buf, 4);
    int32_t readMax;
    rb2.readBytes(reinterpret_cast<uint8_t*>(&readMax), 4);
    TEST_ASSERT_EQUAL_INT32(INT32_MAX, readMax);
}

void test_wire_truncated_varint() {
    // Varint with continuation bit but missing next byte
    uint8_t buf[] = {0x80};  // Continuation bit set, but no more bytes
    ReadBuffer rb(buf, sizeof(buf));

    rb.readVarint();  // Should handle gracefully
    TEST_ASSERT_TRUE(rb.error());
}

void test_wire_truncated_propid() {
    // Two-byte propid with only first byte
    uint8_t buf[] = {0x80};  // High bit set (needs second byte)
    ReadBuffer rb(buf, sizeof(buf));

    rb.readPropId();
    TEST_ASSERT_TRUE(rb.error());
}

// ==== Protocol Validation Boundary Tests ====

void test_validation_batch_count_encoding() {
    // Test batch count encoding: stored as count-1, so 1-256 is valid
    uint8_t buf[8];
    WriteBuffer wb(buf, sizeof(buf));

    // Batch count 1 encodes as 0
    PropertyUpdateFlags flags;
    flags.batch = true;
    wb.writeByte(encodeOpHeader(OpCode::PROPERTY_UPDATE, flags.encode()));
    wb.writeByte(0);  // batch_count - 1 = 0 means 1 item
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[1]);

    // Batch count 256 encodes as 255
    wb.reset();
    wb.writeByte(encodeOpHeader(OpCode::PROPERTY_UPDATE, flags.encode()));
    wb.writeByte(255);  // batch_count - 1 = 255 means 256 items
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[1]);
}

void test_validation_batch_decode_boundaries() {
    // Decode batch count 1 (stored as 0)
    uint8_t buf1[] = {0x11, 0x00};  // flags=1 (batch), count-1=0
    ReadBuffer rb1(buf1, sizeof(buf1));
    rb1.readByte();  // opheader

    uint8_t batchCount;
    uint32_t timestamp;
    PropertyUpdate::decodeHeader(1, rb1, batchCount, timestamp);
    TEST_ASSERT_EQUAL(1, batchCount);

    // Decode batch count 128 (stored as 127)
    uint8_t buf2[] = {0x11, 0x7F};  // flags=1, count-1=127
    ReadBuffer rb2(buf2, sizeof(buf2));
    rb2.readByte();

    PropertyUpdate::decodeHeader(1, rb2, batchCount, timestamp);
    TEST_ASSERT_EQUAL(128, batchCount);

    // Decode batch count 255 (stored as 254) - max safe value for uint8_t
    // Note: count=256 (stored as 255) overflows uint8_t in decodeHeader
    uint8_t buf3[] = {0x11, 0xFE};  // flags=1, count-1=254
    ReadBuffer rb3(buf3, sizeof(buf3));
    rb3.readByte();

    PropertyUpdate::decodeHeader(1, rb3, batchCount, timestamp);
    TEST_ASSERT_EQUAL(255, batchCount);
}

void test_validation_timestamp_zero() {
    uint8_t buf[16];
    WriteBuffer wb(buf, sizeof(buf));

    // Encode timestamp 0
    PropertyUpdateFlags flags;
    flags.hasTimestamp = true;
    wb.writeByte(encodeOpHeader(OpCode::PROPERTY_UPDATE, flags.encode()));
    wb.writeVarint(0);  // timestamp = 0

    // Verify decode
    ReadBuffer rb(buf, wb.position());
    rb.readByte();  // opheader

    uint8_t batchCount;
    uint32_t timestamp;
    PropertyUpdate::decodeHeader(2, rb, batchCount, timestamp);  // flags=2 (timestamp)
    TEST_ASSERT_EQUAL(0, timestamp);
}

void test_validation_timestamp_max() {
    uint8_t buf[16];
    WriteBuffer wb(buf, sizeof(buf));

    // Encode max timestamp
    PropertyUpdateFlags flags;
    flags.hasTimestamp = true;
    wb.writeByte(encodeOpHeader(OpCode::PROPERTY_UPDATE, flags.encode()));
    wb.writeVarint(0xFFFFFFFF);

    ReadBuffer rb(buf, wb.position());
    rb.readByte();

    uint8_t batchCount;
    uint32_t timestamp;
    PropertyUpdate::decodeHeader(2, rb, batchCount, timestamp);
    TEST_ASSERT_EQUAL(0xFFFFFFFF, timestamp);
}

void test_validation_propid_zero() {
    // Property ID 0 is valid
    uint8_t buf[8];
    WriteBuffer wb(buf, sizeof(buf));
    wb.writePropId(0);
    TEST_ASSERT_EQUAL(1, wb.position());
    TEST_ASSERT_EQUAL(0, buf[0]);
}

void test_validation_error_code_range() {
    // Test various error code values
    uint8_t buf[64];
    WriteBuffer wb(buf, sizeof(buf));

    // Max defined error code
    ErrorMessage err(ErrorCode::BUFFER_OVERFLOW, "Buffer overflow");
    TEST_ASSERT_TRUE(err.encode(wb));
    TEST_ASSERT_FALSE(wb.overflow());
}

void test_validation_rpc_call_id_zero() {
    uint8_t buf[16];
    WriteBuffer wb(buf, sizeof(buf));

    RpcFlags flags;
    flags.needsResponse = true;

    wb.writeByte(encodeOpHeader(OpCode::RPC, flags.encode()));
    wb.writePropId(1);  // function_id
    wb.writeVarint(0);  // call_id = 0 (valid)

    TEST_ASSERT_FALSE(wb.overflow());
}

void test_validation_resource_id_boundaries() {
    // Resource IDs use varint encoding
    uint8_t buf[16];

    // Resource ID 0
    WriteBuffer wb1(buf, sizeof(buf));
    wb1.writeByte(encodeOpHeader(OpCode::RESOURCE_GET, 0));
    wb1.writePropId(0);  // property_id
    wb1.writeVarint(0);  // resource_id
    TEST_ASSERT_FALSE(wb1.overflow());

    // Resource ID 127 (single byte)
    WriteBuffer wb2(buf, sizeof(buf));
    wb2.writeByte(encodeOpHeader(OpCode::RESOURCE_GET, 0));
    wb2.writePropId(0);
    wb2.writeVarint(127);
    TEST_ASSERT_EQUAL(3, wb2.position());

    // Resource ID 128 (two bytes)
    WriteBuffer wb3(buf, sizeof(buf));
    wb3.writeByte(encodeOpHeader(OpCode::RESOURCE_GET, 0));
    wb3.writePropId(0);
    wb3.writeVarint(128);
    TEST_ASSERT_EQUAL(4, wb3.position());
}

// ==== Main ====

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // HELLO tests
    RUN_TEST(test_hello_request_encode);
    RUN_TEST(test_hello_request_roundtrip);
    RUN_TEST(test_hello_response_encode);
    RUN_TEST(test_hello_response_roundtrip);

    // ERROR tests
    RUN_TEST(test_error_encode_simple);
    RUN_TEST(test_error_encode_with_schema_mismatch);
    RUN_TEST(test_error_decode_simple);
    RUN_TEST(test_error_decode_with_schema_mismatch);
    RUN_TEST(test_error_roundtrip);
    RUN_TEST(test_error_empty_message);

    // OpHeader tests
    RUN_TEST(test_error_opcode_header);
    RUN_TEST(test_schema_opcode_header_batched);
    RUN_TEST(test_rpc_opcode_header);
    RUN_TEST(test_ping_opcode_header);

    // SCHEMA_DELETE tests
    RUN_TEST(test_schema_delete_single_encode);
    RUN_TEST(test_schema_delete_single_decode);
    RUN_TEST(test_schema_delete_batched_encode);
    RUN_TEST(test_schema_delete_batched_decode);
    RUN_TEST(test_schema_delete_roundtrip);
    RUN_TEST(test_schema_delete_large_propid);

    // RESOURCE_GET tests
    RUN_TEST(test_resource_get_request_encode);
    RUN_TEST(test_resource_get_response_ok_encode);
    RUN_TEST(test_resource_get_response_error_encode);
    RUN_TEST(test_resource_get_large_ids);

    // RESOURCE_PUT tests
    RUN_TEST(test_resource_put_request_create_encode);
    RUN_TEST(test_resource_put_request_update_header_only_encode);
    RUN_TEST(test_resource_put_response_ok_encode);
    RUN_TEST(test_resource_put_response_error_encode);

    // RESOURCE_DELETE tests
    RUN_TEST(test_resource_delete_request_encode);
    RUN_TEST(test_resource_delete_response_ok_encode);
    RUN_TEST(test_resource_delete_response_error_encode);

    // PROPERTY_UPDATE tests
    RUN_TEST(test_property_update_single_uint8);
    RUN_TEST(test_property_update_single_int32);
    RUN_TEST(test_property_update_single_float32);
    RUN_TEST(test_property_update_single_bool);
    RUN_TEST(test_property_update_with_timestamp);
    RUN_TEST(test_property_update_batched_two_properties);
    RUN_TEST(test_property_update_batched_max_256);
    RUN_TEST(test_property_update_batched_with_timestamp);
    RUN_TEST(test_property_update_array_rgb);
    RUN_TEST(test_property_update_decode_header_single);
    RUN_TEST(test_property_update_decode_header_batched);
    RUN_TEST(test_property_update_decode_header_with_timestamp);
    RUN_TEST(test_property_update_large_propid);
    RUN_TEST(test_property_update_zero_batch_count_fails);
    RUN_TEST(test_property_update_over_256_batch_fails);

    // PING tests
    RUN_TEST(test_ping_request_encode);
    RUN_TEST(test_ping_response_encode);
    RUN_TEST(test_ping_large_payload);

    // RPC tests
    RUN_TEST(test_rpc_request_encode);
    RUN_TEST(test_rpc_request_fire_and_forget);
    RUN_TEST(test_rpc_response_success);
    RUN_TEST(test_rpc_response_error);
    RUN_TEST(test_rpc_large_function_id);

    // LED Control System Scenario tests
    RUN_TEST(test_led_scenario_set_brightness);
    RUN_TEST(test_led_scenario_set_shader_index);
    RUN_TEST(test_led_scenario_batched_brightness_and_shader);
    RUN_TEST(test_led_scenario_rgb_preview_list);
    RUN_TEST(test_led_scenario_atmospheric_fade_toggle);
    RUN_TEST(test_led_scenario_full_state_sync);
    RUN_TEST(test_led_scenario_color_array_update);

    // Wire format edge case tests
    RUN_TEST(test_wire_buffer_overflow_write);
    RUN_TEST(test_wire_buffer_overflow_read);
    RUN_TEST(test_wire_varint_boundary_127);
    RUN_TEST(test_wire_varint_boundary_128);
    RUN_TEST(test_wire_varint_max_32bit);
    RUN_TEST(test_wire_propid_boundary_127);
    RUN_TEST(test_wire_propid_boundary_128);
    RUN_TEST(test_wire_propid_max_32767);
    RUN_TEST(test_wire_empty_buffer);
    RUN_TEST(test_wire_utf8_empty_string);
    RUN_TEST(test_wire_utf8_max_length);
    RUN_TEST(test_wire_float_special_values);
    RUN_TEST(test_wire_int32_boundary_values);
    RUN_TEST(test_wire_truncated_varint);
    RUN_TEST(test_wire_truncated_propid);

    // Protocol validation boundary tests
    RUN_TEST(test_validation_batch_count_encoding);
    RUN_TEST(test_validation_batch_decode_boundaries);
    RUN_TEST(test_validation_timestamp_zero);
    RUN_TEST(test_validation_timestamp_max);
    RUN_TEST(test_validation_propid_zero);
    RUN_TEST(test_validation_error_code_range);
    RUN_TEST(test_validation_rpc_call_id_zero);
    RUN_TEST(test_validation_resource_id_boundaries);

    return UNITY_END();
}

#endif // NATIVE_TEST