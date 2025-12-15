#ifdef NATIVE_TEST

#include <unity.h>
#include <wire/Buffer.h>
#include <wire/OpCode.h>
#include <wire/TypeCodec.h>
#include <wire/PropertyUpdate.h>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <climits>

using namespace MicroProto;

// ==== Buffer Tests ====

void test_write_buffer_bytes() {
    uint8_t buf[16];
    WriteBuffer wb(buf, sizeof(buf));

    TEST_ASSERT_TRUE(wb.writeByte(0x42));
    TEST_ASSERT_TRUE(wb.writeByte(0xFF));
    TEST_ASSERT_EQUAL(2, wb.position());
    TEST_ASSERT_TRUE(wb.ok());

    TEST_ASSERT_EQUAL_HEX8(0x42, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[1]);
}

void test_write_buffer_overflow() {
    uint8_t buf[2];
    WriteBuffer wb(buf, sizeof(buf));

    TEST_ASSERT_TRUE(wb.writeByte(0x01));
    TEST_ASSERT_TRUE(wb.writeByte(0x02));
    TEST_ASSERT_FALSE(wb.writeByte(0x03));  // Overflow
    TEST_ASSERT_TRUE(wb.overflow());
    TEST_ASSERT_FALSE(wb.ok());
}

void test_read_buffer_bytes() {
    uint8_t data[] = {0x42, 0xFF, 0x00};
    ReadBuffer rb(data, sizeof(data));

    TEST_ASSERT_EQUAL_HEX8(0x42, rb.readByte());
    TEST_ASSERT_EQUAL_HEX8(0xFF, rb.readByte());
    TEST_ASSERT_EQUAL_HEX8(0x00, rb.readByte());
    TEST_ASSERT_TRUE(rb.ok());

    // Reading past end
    rb.readByte();
    TEST_ASSERT_TRUE(rb.error());
}

// ==== Varint Tests ====

void test_varint_single_byte() {
    uint8_t buf[8];
    WriteBuffer wb(buf, sizeof(buf));

    // 0
    wb.writeVarint(0);
    TEST_ASSERT_EQUAL(1, wb.position());
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[0]);

    // 127 (max single byte)
    wb.reset();
    wb.writeVarint(127);
    TEST_ASSERT_EQUAL(1, wb.position());
    TEST_ASSERT_EQUAL_HEX8(0x7F, buf[0]);
}

void test_varint_two_bytes() {
    uint8_t buf[8];
    WriteBuffer wb(buf, sizeof(buf));

    // 128 (first two-byte)
    wb.writeVarint(128);
    TEST_ASSERT_EQUAL(2, wb.position());
    TEST_ASSERT_EQUAL_HEX8(0x80, buf[0]);  // 128 & 0x7F = 0, with continuation
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[1]);  // 128 >> 7 = 1

    // 16383 (max two byte)
    wb.reset();
    wb.writeVarint(16383);
    TEST_ASSERT_EQUAL(2, wb.position());
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x7F, buf[1]);
}

void test_varint_roundtrip() {
    uint8_t buf[8];

    uint32_t testValues[] = {0, 1, 127, 128, 255, 256, 16383, 16384, 65535, 1000000, 0xFFFFFFFF};

    for (uint32_t val : testValues) {
        WriteBuffer wb(buf, sizeof(buf));
        wb.writeVarint(val);

        ReadBuffer rb(buf, wb.position());
        uint32_t decoded = rb.readVarint();

        TEST_ASSERT_TRUE(rb.ok());
        TEST_ASSERT_EQUAL_UINT32(val, decoded);
    }
}

// ==== Integer Type Tests ====

void test_uint8_roundtrip() {
    uint8_t buf[8];
    uint8_t testValues[] = {0, 1, 127, 128, 200, 255};

    for (uint8_t val : testValues) {
        WriteBuffer wb(buf, sizeof(buf));
        wb.writeUint8(val);

        ReadBuffer rb(buf, wb.position());
        uint8_t decoded = rb.readUint8();

        TEST_ASSERT_TRUE(rb.ok());
        TEST_ASSERT_EQUAL_UINT8(val, decoded);
    }
}

void test_int8_roundtrip() {
    uint8_t buf[8];
    int8_t testValues[] = {-128, -1, 0, 1, 127};

    for (int8_t val : testValues) {
        WriteBuffer wb(buf, sizeof(buf));
        wb.writeInt8(val);

        ReadBuffer rb(buf, wb.position());
        int8_t decoded = rb.readInt8();

        TEST_ASSERT_TRUE(rb.ok());
        TEST_ASSERT_EQUAL_INT8(val, decoded);
    }
}

void test_uint16_roundtrip() {
    uint8_t buf[8];
    uint16_t testValues[] = {0, 1, 255, 256, 0x1234, 0xFFFF};

    for (uint16_t val : testValues) {
        WriteBuffer wb(buf, sizeof(buf));
        wb.writeUint16(val);

        TEST_ASSERT_EQUAL(2, wb.position());

        ReadBuffer rb(buf, wb.position());
        uint16_t decoded = rb.readUint16();

        TEST_ASSERT_TRUE(rb.ok());
        TEST_ASSERT_EQUAL_UINT16(val, decoded);
    }
}

void test_int32_roundtrip() {
    uint8_t buf[8];
    int32_t testValues[] = {INT32_MIN, -1, 0, 1, INT32_MAX, 0x12345678};

    for (int32_t val : testValues) {
        WriteBuffer wb(buf, sizeof(buf));
        wb.writeInt32(val);

        TEST_ASSERT_EQUAL(4, wb.position());

        ReadBuffer rb(buf, wb.position());
        int32_t decoded = rb.readInt32();

        TEST_ASSERT_TRUE(rb.ok());
        TEST_ASSERT_EQUAL_INT32(val, decoded);
    }
}

void test_float32_roundtrip() {
    uint8_t buf[8];
    float testValues[] = {0.0f, 1.0f, -1.0f, 3.14159f, -273.15f, 1e10f, 1e-10f};

    for (float val : testValues) {
        WriteBuffer wb(buf, sizeof(buf));
        wb.writeFloat32(val);

        TEST_ASSERT_EQUAL(4, wb.position());

        ReadBuffer rb(buf, wb.position());
        float decoded = rb.readFloat32();

        TEST_ASSERT_TRUE(rb.ok());
        TEST_ASSERT_FLOAT_WITHIN(0.0001f * fabsf(val), val, decoded);
    }
}

void test_bool_roundtrip() {
    uint8_t buf[8];

    WriteBuffer wb(buf, sizeof(buf));
    wb.writeBool(true);
    wb.writeBool(false);

    ReadBuffer rb(buf, wb.position());
    TEST_ASSERT_TRUE(rb.readBool());
    TEST_ASSERT_FALSE(rb.readBool());
    TEST_ASSERT_TRUE(rb.ok());
}

// ==== Little-Endian Tests ====

void test_uint16_little_endian() {
    uint8_t buf[2];
    WriteBuffer wb(buf, sizeof(buf));

    wb.writeUint16(0x1234);

    // Little-endian: low byte first
    TEST_ASSERT_EQUAL_HEX8(0x34, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x12, buf[1]);
}

void test_uint32_little_endian() {
    uint8_t buf[4];
    WriteBuffer wb(buf, sizeof(buf));

    wb.writeUint32(0x12345678);

    // Little-endian: low byte first
    TEST_ASSERT_EQUAL_HEX8(0x78, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x56, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x34, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x12, buf[3]);
}

// ==== OpHeader Tests ====

void test_opheader_encode_decode() {
    OpHeader h1(OpCode::PROPERTY_UPDATE_SHORT, 0, false);
    uint8_t encoded = h1.encode();
    TEST_ASSERT_EQUAL_HEX8(0x01, encoded);  // opcode=1, flags=0, batch=0

    OpHeader decoded = OpHeader::decode(encoded);
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(OpCode::PROPERTY_UPDATE_SHORT),
                      decoded.opcode);
    TEST_ASSERT_EQUAL(0, decoded.flags);
    TEST_ASSERT_EQUAL(0, decoded.batch);
}

void test_opheader_with_batch() {
    OpHeader h(OpCode::PROPERTY_UPDATE_SHORT, 0, true);
    uint8_t encoded = h.encode();
    TEST_ASSERT_EQUAL_HEX8(0x81, encoded);  // opcode=1, flags=0, batch=1

    OpHeader decoded = OpHeader::decode(encoded);
    TEST_ASSERT_EQUAL(1, decoded.batch);
}

void test_opheader_with_flags() {
    OpHeader h(OpCode::HELLO, 5, false);
    uint8_t encoded = h.encode();
    TEST_ASSERT_EQUAL_HEX8(0x50, encoded);  // opcode=0, flags=5, batch=0

    OpHeader decoded = OpHeader::decode(encoded);
    TEST_ASSERT_EQUAL(0, decoded.opcode);
    TEST_ASSERT_EQUAL(5, decoded.flags);
}

// ==== TypeCodec Tests ====

void test_type_codec_uint8() {
    uint8_t buf[8];
    WriteBuffer wb(buf, sizeof(buf));

    uint8_t val = 200;
    TEST_ASSERT_TRUE(TypeCodec::encode(wb, TYPE_UINT8, &val, sizeof(val)));
    TEST_ASSERT_EQUAL(1, wb.position());

    ReadBuffer rb(buf, wb.position());
    uint8_t decoded;
    TEST_ASSERT_TRUE(TypeCodec::decode(rb, TYPE_UINT8, &decoded, sizeof(decoded)));
    TEST_ASSERT_EQUAL_UINT8(200, decoded);
}

void test_type_codec_int32() {
    uint8_t buf[8];
    WriteBuffer wb(buf, sizeof(buf));

    int32_t val = -12345;
    TEST_ASSERT_TRUE(TypeCodec::encode(wb, TYPE_INT32, &val, sizeof(val)));
    TEST_ASSERT_EQUAL(4, wb.position());

    ReadBuffer rb(buf, wb.position());
    int32_t decoded;
    TEST_ASSERT_TRUE(TypeCodec::decode(rb, TYPE_INT32, &decoded, sizeof(decoded)));
    TEST_ASSERT_EQUAL_INT32(-12345, decoded);
}

void test_type_codec_float32() {
    uint8_t buf[8];
    WriteBuffer wb(buf, sizeof(buf));

    float val = 3.14159f;
    TEST_ASSERT_TRUE(TypeCodec::encode(wb, TYPE_FLOAT32, &val, sizeof(val)));

    ReadBuffer rb(buf, wb.position());
    float decoded;
    TEST_ASSERT_TRUE(TypeCodec::decode(rb, TYPE_FLOAT32, &decoded, sizeof(decoded)));
    TEST_ASSERT_FLOAT_WITHIN(0.00001f, 3.14159f, decoded);
}

void test_type_codec_bool() {
    uint8_t buf[8];
    WriteBuffer wb(buf, sizeof(buf));

    bool val = true;
    TEST_ASSERT_TRUE(TypeCodec::encode(wb, TYPE_BOOL, &val, sizeof(val)));

    ReadBuffer rb(buf, wb.position());
    bool decoded;
    TEST_ASSERT_TRUE(TypeCodec::decode(rb, TYPE_BOOL, &decoded, sizeof(decoded)));
    TEST_ASSERT_TRUE(decoded);
}

void test_type_size() {
    TEST_ASSERT_EQUAL(1, TypeCodec::typeSize(TYPE_BOOL));
    TEST_ASSERT_EQUAL(1, TypeCodec::typeSize(TYPE_INT8));
    TEST_ASSERT_EQUAL(1, TypeCodec::typeSize(TYPE_UINT8));
    TEST_ASSERT_EQUAL(4, TypeCodec::typeSize(TYPE_INT32));
    TEST_ASSERT_EQUAL(4, TypeCodec::typeSize(TYPE_FLOAT32));
    TEST_ASSERT_EQUAL(0, TypeCodec::typeSize(0xFF));  // Unknown
}

// ==== PropertyUpdate Encoding Tests (without PropertyBase) ====

void test_property_update_encode_short() {
    uint8_t buf[16];
    WriteBuffer wb(buf, sizeof(buf));

    uint8_t val = 128;
    TEST_ASSERT_TRUE(PropertyUpdate::encodeShortValue(wb, 1, TYPE_UINT8, &val, 1));

    // Expected: header(0x01) + propId(0x01) + flags(0x00) + value(0x80)
    TEST_ASSERT_EQUAL(4, wb.position());
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[0]);  // PROPERTY_UPDATE_SHORT, no batch
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[1]);  // property_id = 1
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[2]);  // flags = 0
    TEST_ASSERT_EQUAL_HEX8(0x80, buf[3]);  // value = 128
}

void test_property_update_encode_int32() {
    uint8_t buf[16];
    WriteBuffer wb(buf, sizeof(buf));

    int32_t val = 0x12345678;
    TEST_ASSERT_TRUE(PropertyUpdate::encodeShortValue(wb, 5, TYPE_INT32, &val, 4));

    // header + propId + flags + 4 bytes value
    TEST_ASSERT_EQUAL(7, wb.position());
    TEST_ASSERT_EQUAL_HEX8(0x05, buf[1]);  // property_id = 5

    // Little-endian value
    TEST_ASSERT_EQUAL_HEX8(0x78, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x56, buf[4]);
    TEST_ASSERT_EQUAL_HEX8(0x34, buf[5]);
    TEST_ASSERT_EQUAL_HEX8(0x12, buf[6]);
}

void test_property_update_decode_short() {
    // Manually construct a message
    uint8_t msg[] = {
        0x01,  // PROPERTY_UPDATE_SHORT, no batch
        0x03,  // property_id = 3
        0x00,  // flags = 0
        0xAB   // value = 171
    };

    ReadBuffer rb(msg, sizeof(msg));

    uint8_t propId;
    PropertyUpdateFlags flags;
    uint8_t value;
    size_t valueSize;

    TEST_ASSERT_TRUE(PropertyUpdate::decodeShort(rb, propId, flags, &value, valueSize, TYPE_UINT8));
    TEST_ASSERT_EQUAL_UINT8(3, propId);
    TEST_ASSERT_EQUAL_UINT8(171, value);
}

// ==== Setup/Teardown ====

void setUp(void) {}
void tearDown(void) {}

// ==== Main ====

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // Buffer tests
    RUN_TEST(test_write_buffer_bytes);
    RUN_TEST(test_write_buffer_overflow);
    RUN_TEST(test_read_buffer_bytes);

    // Varint tests
    RUN_TEST(test_varint_single_byte);
    RUN_TEST(test_varint_two_bytes);
    RUN_TEST(test_varint_roundtrip);

    // Integer type tests
    RUN_TEST(test_uint8_roundtrip);
    RUN_TEST(test_int8_roundtrip);
    RUN_TEST(test_uint16_roundtrip);
    RUN_TEST(test_int32_roundtrip);
    RUN_TEST(test_float32_roundtrip);
    RUN_TEST(test_bool_roundtrip);

    // Little-endian tests
    RUN_TEST(test_uint16_little_endian);
    RUN_TEST(test_uint32_little_endian);

    // OpHeader tests
    RUN_TEST(test_opheader_encode_decode);
    RUN_TEST(test_opheader_with_batch);
    RUN_TEST(test_opheader_with_flags);

    // TypeCodec tests
    RUN_TEST(test_type_codec_uint8);
    RUN_TEST(test_type_codec_int32);
    RUN_TEST(test_type_codec_float32);
    RUN_TEST(test_type_codec_bool);
    RUN_TEST(test_type_size);

    // PropertyUpdate tests
    RUN_TEST(test_property_update_encode_short);
    RUN_TEST(test_property_update_encode_int32);
    RUN_TEST(test_property_update_decode_short);

    return UNITY_END();
}

#endif // NATIVE_TEST
