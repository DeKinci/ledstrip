// Matter TLV encoding/decoding tests
// Reference: Matter Core Specification, Appendix A — TLV Encoding Examples

#include <unity.h>
#include "MatterTLV.h"

using namespace matter;

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// TLVWriter — Encoding
// ============================================================================

void test_tlv_encode_bool_true() {
    uint8_t buf[8];
    TLVWriter w(buf, sizeof(buf));
    w.putBool(0, true);
    // Context tag 0 + True = 0x29 0x00
    TEST_ASSERT_EQUAL(2, w.size());
    TEST_ASSERT_EQUAL(kTagContext | kTLVTrue, buf[0]);
    TEST_ASSERT_EQUAL(0, buf[1]);  // tag number
}

void test_tlv_encode_bool_false() {
    uint8_t buf[8];
    TLVWriter w(buf, sizeof(buf));
    w.putBool(1, false);
    TEST_ASSERT_EQUAL(2, w.size());
    TEST_ASSERT_EQUAL(kTagContext | kTLVFalse, buf[0]);
    TEST_ASSERT_EQUAL(1, buf[1]);
}

void test_tlv_encode_u8() {
    uint8_t buf[8];
    TLVWriter w(buf, sizeof(buf));
    w.putU8(kAnon, 42);
    // Anonymous tag + UInt8 = 0x04, value = 42
    TEST_ASSERT_EQUAL(2, w.size());
    TEST_ASSERT_EQUAL(kTagAnonymous | kTLVUInt8, buf[0]);
    TEST_ASSERT_EQUAL(42, buf[1]);
}

void test_tlv_encode_u16() {
    uint8_t buf[8];
    TLVWriter w(buf, sizeof(buf));
    w.putU16(kAnon, 0x1234);
    TEST_ASSERT_EQUAL(3, w.size());
    TEST_ASSERT_EQUAL(kTagAnonymous | kTLVUInt16, buf[0]);
    TEST_ASSERT_EQUAL(0x34, buf[1]);  // LE
    TEST_ASSERT_EQUAL(0x12, buf[2]);
}

void test_tlv_encode_u32() {
    uint8_t buf[16];
    TLVWriter w(buf, sizeof(buf));
    w.putU32(kAnon, 0xDEADBEEF);
    TEST_ASSERT_EQUAL(5, w.size());
    TEST_ASSERT_EQUAL(0xEF, buf[1]);
    TEST_ASSERT_EQUAL(0xBE, buf[2]);
    TEST_ASSERT_EQUAL(0xAD, buf[3]);
    TEST_ASSERT_EQUAL(0xDE, buf[4]);
}

void test_tlv_encode_i32_negative() {
    uint8_t buf[16];
    TLVWriter w(buf, sizeof(buf));
    w.putI32(kAnon, -1);
    TEST_ASSERT_EQUAL(5, w.size());
    TEST_ASSERT_EQUAL(kTagAnonymous | kTLVInt32, buf[0]);
    TEST_ASSERT_EQUAL(0xFF, buf[1]);
    TEST_ASSERT_EQUAL(0xFF, buf[2]);
    TEST_ASSERT_EQUAL(0xFF, buf[3]);
    TEST_ASSERT_EQUAL(0xFF, buf[4]);
}

void test_tlv_encode_string() {
    uint8_t buf[32];
    TLVWriter w(buf, sizeof(buf));
    w.putString(kAnon, "Hello");
    // Anonymous UTF8-1byte: control(1) + length(1) + "Hello"(5)
    TEST_ASSERT_EQUAL(7, w.size());
    TEST_ASSERT_EQUAL(kTagAnonymous | kTLVUtf8_1, buf[0]);
    TEST_ASSERT_EQUAL(5, buf[1]);
    TEST_ASSERT_EQUAL('H', buf[2]);
    TEST_ASSERT_EQUAL('o', buf[6]);
}

void test_tlv_encode_bytes() {
    uint8_t buf[32];
    TLVWriter w(buf, sizeof(buf));
    uint8_t data[] = {0xAA, 0xBB, 0xCC};
    w.putBytes(kAnon, data, 3);
    TEST_ASSERT_EQUAL(5, w.size());
    TEST_ASSERT_EQUAL(kTagAnonymous | kTLVBytes1, buf[0]);
    TEST_ASSERT_EQUAL(3, buf[1]);
    TEST_ASSERT_EQUAL(0xAA, buf[2]);
    TEST_ASSERT_EQUAL(0xCC, buf[4]);
}

void test_tlv_encode_null() {
    uint8_t buf[8];
    TLVWriter w(buf, sizeof(buf));
    w.putNull(2);
    TEST_ASSERT_EQUAL(2, w.size());
    TEST_ASSERT_EQUAL(kTagContext | kTLVNull, buf[0]);
    TEST_ASSERT_EQUAL(2, buf[1]);
}

void test_tlv_encode_struct() {
    uint8_t buf[32];
    TLVWriter w(buf, sizeof(buf));
    w.openStruct(kAnon);
    w.putU8(0, 1);
    w.putBool(1, true);
    w.closeContainer();
    // Struct(0x15) + U8(0x24 0x00 0x01) + True(0x29 0x01) + End(0x18)
    TEST_ASSERT_EQUAL(7, w.size());
    TEST_ASSERT_EQUAL(kTLVStruct, buf[0]);
    TEST_ASSERT_EQUAL(kTLVEnd, buf[6]);
}

void test_tlv_encode_nested_struct() {
    uint8_t buf[64];
    TLVWriter w(buf, sizeof(buf));
    w.openStruct(kAnon);
      w.openStruct(0);
        w.putU8(0, 42);
      w.closeContainer();
    w.closeContainer();
    TEST_ASSERT_FALSE(w.error());
    // Outer end
    TEST_ASSERT_EQUAL(kTLVEnd, buf[w.size() - 1]);
}

void test_tlv_encode_array() {
    uint8_t buf[32];
    TLVWriter w(buf, sizeof(buf));
    w.openArray(kAnon);
    w.putU8(kAnon, 1);
    w.putU8(kAnon, 2);
    w.putU8(kAnon, 3);
    w.closeContainer();
    // Array + 3 anonymous U8s + End
    TEST_ASSERT_EQUAL(8, w.size());
    TEST_ASSERT_FALSE(w.error());
}

void test_tlv_encode_overflow() {
    uint8_t buf[2];
    TLVWriter w(buf, sizeof(buf));
    w.putU32(kAnon, 0x12345678);
    TEST_ASSERT_TRUE(w.error());
}

void test_tlv_encode_context_tag() {
    uint8_t buf[8];
    TLVWriter w(buf, sizeof(buf));
    w.putU8(42, 0);
    // Context tag 42: control(1) + tag(1) + value(1) = 3 bytes
    TEST_ASSERT_EQUAL(3, w.size());
    TEST_ASSERT_EQUAL(kTagContext | kTLVUInt8, buf[0]);
    TEST_ASSERT_EQUAL(42, buf[1]);
    TEST_ASSERT_EQUAL(0, buf[2]);
}

void test_tlv_encode_float() {
    uint8_t buf[16];
    TLVWriter w(buf, sizeof(buf));
    w.putFloat(kAnon, 3.14f);
    TEST_ASSERT_EQUAL(5, w.size());
    // Verify by decoding
    float f;
    memcpy(&f, buf + 1, 4);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.14f, f);
}

// ============================================================================
// TLVReader — Decoding
// ============================================================================

void test_tlv_decode_u8() {
    uint8_t data[] = {kTagAnonymous | kTLVUInt8, 42};
    TLVReader r(data, sizeof(data));
    TEST_ASSERT_TRUE(r.next());
    TEST_ASSERT_EQUAL(kTLVUInt8, r.type());
    TEST_ASSERT_EQUAL(kAnon, r.tag());
    TEST_ASSERT_EQUAL(42, r.getU8());
}

void test_tlv_decode_u16() {
    uint8_t data[] = {kTagAnonymous | kTLVUInt16, 0x34, 0x12};
    TLVReader r(data, sizeof(data));
    TEST_ASSERT_TRUE(r.next());
    TEST_ASSERT_EQUAL(0x1234, r.getU16());
}

void test_tlv_decode_u32() {
    uint8_t data[] = {kTagAnonymous | kTLVUInt32, 0xEF, 0xBE, 0xAD, 0xDE};
    TLVReader r(data, sizeof(data));
    TEST_ASSERT_TRUE(r.next());
    TEST_ASSERT_EQUAL(0xDEADBEEF, r.getU32());
}

void test_tlv_decode_bool() {
    uint8_t data[] = {kTagContext | kTLVTrue, 0};
    TLVReader r(data, sizeof(data));
    TEST_ASSERT_TRUE(r.next());
    TEST_ASSERT_EQUAL(0, r.tag());
    TEST_ASSERT_TRUE(r.getBool());
}

void test_tlv_decode_string() {
    uint8_t data[] = {kTagAnonymous | kTLVUtf8_1, 3, 'a', 'b', 'c'};
    TLVReader r(data, sizeof(data));
    TEST_ASSERT_TRUE(r.next());
    size_t len;
    const uint8_t* s = r.getBytes(len);
    TEST_ASSERT_EQUAL(3, len);
    TEST_ASSERT_EQUAL('a', s[0]);
    TEST_ASSERT_EQUAL('c', s[2]);
}

void test_tlv_decode_struct() {
    // {0: 42, 1: true}
    uint8_t data[] = {
        kTLVStruct,
        kTagContext | kTLVUInt8, 0, 42,
        kTagContext | kTLVTrue, 1,
        kTLVEnd
    };
    TLVReader r(data, sizeof(data));

    TEST_ASSERT_TRUE(r.next());
    TEST_ASSERT_TRUE(r.isStruct());

    TEST_ASSERT_TRUE(r.next());
    TEST_ASSERT_EQUAL(0, r.tag());
    TEST_ASSERT_EQUAL(42, r.getU8());

    TEST_ASSERT_TRUE(r.next());
    TEST_ASSERT_EQUAL(1, r.tag());
    TEST_ASSERT_TRUE(r.getBool());

    TEST_ASSERT_TRUE(r.next());
    TEST_ASSERT_TRUE(r.isEnd());
}

void test_tlv_decode_skip_container() {
    // {0: {1: 42}, 2: 99}
    uint8_t data[] = {
        kTLVStruct,
          kTagContext | kTLVStruct, 0,
            kTagContext | kTLVUInt8, 1, 42,
          kTLVEnd,
          kTagContext | kTLVUInt8, 2, 99,
        kTLVEnd
    };
    TLVReader r(data, sizeof(data));

    TEST_ASSERT_TRUE(r.next());  // outer struct
    TEST_ASSERT_TRUE(r.next());  // inner struct (tag 0)
    r.skipContainer();            // skip inner struct contents

    TEST_ASSERT_TRUE(r.next());  // should be at tag 2 = 99
    TEST_ASSERT_EQUAL(2, r.tag());
    TEST_ASSERT_EQUAL(99, r.getU8());
}

void test_tlv_decode_array() {
    uint8_t data[] = {
        kTLVArray,
        kTagAnonymous | kTLVUInt8, 10,
        kTagAnonymous | kTLVUInt8, 20,
        kTagAnonymous | kTLVUInt8, 30,
        kTLVEnd
    };
    TLVReader r(data, sizeof(data));

    TEST_ASSERT_TRUE(r.next());
    TEST_ASSERT_TRUE(r.isArray());

    TEST_ASSERT_TRUE(r.next());
    TEST_ASSERT_EQUAL(10, r.getU8());
    TEST_ASSERT_TRUE(r.next());
    TEST_ASSERT_EQUAL(20, r.getU8());
    TEST_ASSERT_TRUE(r.next());
    TEST_ASSERT_EQUAL(30, r.getU8());

    TEST_ASSERT_TRUE(r.next());
    TEST_ASSERT_TRUE(r.isEnd());
}

// ============================================================================
// Roundtrip — write then read
// ============================================================================

void test_tlv_roundtrip_struct() {
    uint8_t buf[64];
    TLVWriter w(buf, sizeof(buf));
    w.openStruct(kAnon);
    w.putU32(0, 0x12345678);
    w.putBool(1, true);
    w.putString(2, "test");
    w.putI32(3, -100);
    w.closeContainer();
    TEST_ASSERT_FALSE(w.error());

    TLVReader r(buf, w.size());
    TEST_ASSERT_TRUE(r.next());
    TEST_ASSERT_TRUE(r.isStruct());

    TEST_ASSERT_TRUE(r.next());
    TEST_ASSERT_EQUAL(0, r.tag());
    TEST_ASSERT_EQUAL(0x12345678, r.getU32());

    TEST_ASSERT_TRUE(r.next());
    TEST_ASSERT_EQUAL(1, r.tag());
    TEST_ASSERT_TRUE(r.getBool());

    TEST_ASSERT_TRUE(r.next());
    TEST_ASSERT_EQUAL(2, r.tag());
    size_t slen;
    const uint8_t* sdata = r.getBytes(slen);
    TEST_ASSERT_EQUAL(4, slen);
    TEST_ASSERT_EQUAL_MEMORY("test", sdata, 4);

    TEST_ASSERT_TRUE(r.next());
    TEST_ASSERT_EQUAL(3, r.tag());
    TEST_ASSERT_EQUAL(-100, r.getI32());

    TEST_ASSERT_TRUE(r.next());
    TEST_ASSERT_TRUE(r.isEnd());
}

void test_tlv_roundtrip_nested() {
    uint8_t buf[128];
    TLVWriter w(buf, sizeof(buf));
    w.openStruct(kAnon);
      w.openArray(0);
        w.putU8(kAnon, 1);
        w.putU8(kAnon, 2);
      w.closeContainer();
      w.openStruct(1);
        w.putBool(0, false);
      w.closeContainer();
    w.closeContainer();
    TEST_ASSERT_FALSE(w.error());

    TLVReader r(buf, w.size());
    r.next(); // outer struct
    TEST_ASSERT_TRUE(r.isStruct());

    r.next(); // array tag 0
    TEST_ASSERT_EQUAL(0, r.tag());
    TEST_ASSERT_TRUE(r.isArray());

    r.next(); TEST_ASSERT_EQUAL(1, r.getU8());
    r.next(); TEST_ASSERT_EQUAL(2, r.getU8());
    r.next(); TEST_ASSERT_TRUE(r.isEnd()); // array end

    r.next(); // struct tag 1
    TEST_ASSERT_EQUAL(1, r.tag());
    TEST_ASSERT_TRUE(r.isStruct());

    r.next(); TEST_ASSERT_FALSE(r.getBool());
    r.next(); TEST_ASSERT_TRUE(r.isEnd()); // inner struct end
    r.next(); TEST_ASSERT_TRUE(r.isEnd()); // outer struct end
}

// ============================================================================
// Matter Spec — TLV Test Vectors (Appendix A)
// ============================================================================

void test_tlv_spec_signed_integer_1byte() {
    // Matter spec: Signed Integer, 1-octet, value 42
    // Expected: 0x00 0x2A
    uint8_t buf[8];
    TLVWriter w(buf, sizeof(buf));
    w.putI8(kAnon, 42);
    TEST_ASSERT_EQUAL(2, w.size());
    TEST_ASSERT_EQUAL(0x00, buf[0]); // Anonymous + Int8
    TEST_ASSERT_EQUAL(0x2A, buf[1]);
}

void test_tlv_spec_signed_integer_negative() {
    // Signed Integer, 1-octet, value -17
    uint8_t buf[8];
    TLVWriter w(buf, sizeof(buf));
    w.putI8(kAnon, -17);
    TEST_ASSERT_EQUAL(2, w.size());
    TEST_ASSERT_EQUAL(0xEF, buf[1]); // Two's complement of -17
}

void test_tlv_spec_empty_struct() {
    // Empty structure: 0x15 0x18
    uint8_t buf[8];
    TLVWriter w(buf, sizeof(buf));
    w.openStruct(kAnon);
    w.closeContainer();
    TEST_ASSERT_EQUAL(2, w.size());
    TEST_ASSERT_EQUAL(0x15, buf[0]); // Anonymous Struct
    TEST_ASSERT_EQUAL(0x18, buf[1]); // End
}

void test_tlv_spec_empty_array() {
    // Empty array: 0x16 0x18
    uint8_t buf[8];
    TLVWriter w(buf, sizeof(buf));
    w.openArray(kAnon);
    w.closeContainer();
    TEST_ASSERT_EQUAL(2, w.size());
    TEST_ASSERT_EQUAL(0x16, buf[0]);
    TEST_ASSERT_EQUAL(0x18, buf[1]);
}

// ============================================================================
// Matter Spec Appendix A — Additional TLV Test Vectors
// ============================================================================

// A.2.1: Unsigned Integer, 1-octet, value 42
void test_tlv_spec_uint8_42() {
    uint8_t data[] = {0x04, 42}; // Anonymous UInt8
    TLVReader r(data, sizeof(data));
    TEST_ASSERT_TRUE(r.next());
    TEST_ASSERT_EQUAL(kTLVUInt8, r.type());
    TEST_ASSERT_EQUAL(42, r.getU8());
}

// A.2.2: Signed Integer, 2-octet, value -256
void test_tlv_spec_int16_neg256() {
    uint8_t buf[8];
    TLVWriter w(buf, sizeof(buf));
    w.putI16(kAnon, -256);
    TEST_ASSERT_EQUAL(3, w.size());
    // -256 in LE 2's complement: 0x00 0xFF
    TEST_ASSERT_EQUAL(0x00, buf[1]);
    TEST_ASSERT_EQUAL(0xFF, buf[2]);

    TLVReader r(buf, w.size());
    TEST_ASSERT_TRUE(r.next());
    uint16_t raw = r.getU16();
    TEST_ASSERT_EQUAL(-256, (int16_t)raw);
}

// A.3: Boolean True with context tag 1
void test_tlv_spec_bool_context_tag() {
    uint8_t data[] = {kTagContext | kTLVTrue, 1};
    TLVReader r(data, sizeof(data));
    TEST_ASSERT_TRUE(r.next());
    TEST_ASSERT_EQUAL(1, r.tag());
    TEST_ASSERT_TRUE(r.getBool());
}

// A.4: UTF-8 String, 1-octet length, "Hello!"
void test_tlv_spec_utf8_hello() {
    uint8_t buf[16];
    TLVWriter w(buf, sizeof(buf));
    w.putString(kAnon, "Hello!");

    TEST_ASSERT_EQUAL(8, w.size()); // ctrl(1) + len(1) + 6 chars
    TEST_ASSERT_EQUAL(kTLVUtf8_1, buf[0] & 0x1F);
    TEST_ASSERT_EQUAL(6, buf[1]);

    TLVReader r(buf, w.size());
    TEST_ASSERT_TRUE(r.next());
    size_t len;
    const uint8_t* s = r.getBytes(len);
    TEST_ASSERT_EQUAL(6, len);
    TEST_ASSERT_EQUAL_MEMORY("Hello!", s, 6);
}

// A.5: Octet String (bytes), length 4
void test_tlv_spec_octet_string() {
    uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t buf[16];
    TLVWriter w(buf, sizeof(buf));
    w.putBytes(kAnon, payload, 4);

    TLVReader r(buf, w.size());
    TEST_ASSERT_TRUE(r.next());
    size_t len;
    const uint8_t* d = r.getBytes(len);
    TEST_ASSERT_EQUAL(4, len);
    TEST_ASSERT_EQUAL_MEMORY(payload, d, 4);
}

// A.6: Null with context tag
void test_tlv_spec_null_context() {
    uint8_t data[] = {kTagContext | kTLVNull, 5};
    TLVReader r(data, sizeof(data));
    TEST_ASSERT_TRUE(r.next());
    TEST_ASSERT_EQUAL(5, r.tag());
    TEST_ASSERT_EQUAL(kTLVNull, r.type());
}

// A.7: Float32 (IEEE 754)
void test_tlv_spec_float32() {
    // 17.9 in IEEE 754: 0x418F3333 (LE: 33 33 8F 41)
    float val = 17.9f;
    uint8_t buf[16];
    TLVWriter w(buf, sizeof(buf));
    w.putFloat(kAnon, val);
    TEST_ASSERT_EQUAL(5, w.size());

    TLVReader r(buf, w.size());
    TEST_ASSERT_TRUE(r.next());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 17.9f, r.getFloat());
}

// A.8: Struct with multiple typed fields
void test_tlv_spec_struct_mixed_types() {
    uint8_t buf[64];
    TLVWriter w(buf, sizeof(buf));
    w.openStruct(kAnon);
    w.putU8(0, 1);
    w.putBool(1, true);
    w.putU32(2, 0x01020304);
    w.putString(3, "hi");
    w.putNull(4);
    w.closeContainer();
    TEST_ASSERT_FALSE(w.error());

    TLVReader r(buf, w.size());
    TEST_ASSERT_TRUE(r.next()); TEST_ASSERT_TRUE(r.isStruct());

    TEST_ASSERT_TRUE(r.next()); TEST_ASSERT_EQUAL(0, r.tag()); TEST_ASSERT_EQUAL(1, r.getU8());
    TEST_ASSERT_TRUE(r.next()); TEST_ASSERT_EQUAL(1, r.tag()); TEST_ASSERT_TRUE(r.getBool());
    TEST_ASSERT_TRUE(r.next()); TEST_ASSERT_EQUAL(2, r.tag()); TEST_ASSERT_EQUAL(0x01020304, r.getU32());
    TEST_ASSERT_TRUE(r.next()); TEST_ASSERT_EQUAL(3, r.tag());
    size_t len; r.getBytes(len); TEST_ASSERT_EQUAL(2, len);
    TEST_ASSERT_TRUE(r.next()); TEST_ASSERT_EQUAL(4, r.tag()); TEST_ASSERT_EQUAL(kTLVNull, r.type());
    TEST_ASSERT_TRUE(r.next()); TEST_ASSERT_TRUE(r.isEnd());
}

// A.9: Array of structs (common in Matter — AttributeReportIBs)
void test_tlv_spec_array_of_structs() {
    uint8_t buf[64];
    TLVWriter w(buf, sizeof(buf));
    w.openArray(kAnon);
      w.openStruct(kAnon);
        w.putU8(0, 10);
      w.closeContainer();
      w.openStruct(kAnon);
        w.putU8(0, 20);
      w.closeContainer();
    w.closeContainer();
    TEST_ASSERT_FALSE(w.error());

    TLVReader r(buf, w.size());
    r.next(); TEST_ASSERT_TRUE(r.isArray());

    r.next(); TEST_ASSERT_TRUE(r.isStruct());
    r.next(); TEST_ASSERT_EQUAL(10, r.getU8());
    r.next(); TEST_ASSERT_TRUE(r.isEnd());

    r.next(); TEST_ASSERT_TRUE(r.isStruct());
    r.next(); TEST_ASSERT_EQUAL(20, r.getU8());
    r.next(); TEST_ASSERT_TRUE(r.isEnd());

    r.next(); TEST_ASSERT_TRUE(r.isEnd()); // array end
}

// A.10: List (heterogeneous container, used in Matter paths)
void test_tlv_spec_list() {
    uint8_t buf[32];
    TLVWriter w(buf, sizeof(buf));
    w.openList(kAnon);
    w.putU8(kAnon, 1);
    w.putBool(kAnon, false);
    w.putString(kAnon, "x");
    w.closeContainer();

    TLVReader r(buf, w.size());
    r.next(); TEST_ASSERT_TRUE(r.isList());
    r.next(); TEST_ASSERT_EQUAL(1, r.getU8());
    r.next(); TEST_ASSERT_FALSE(r.getBool());
    r.next(); size_t len; r.getBytes(len); TEST_ASSERT_EQUAL(1, len);
    r.next(); TEST_ASSERT_TRUE(r.isEnd());
}

// A.11: Deeply nested (3 levels) — stress test
void test_tlv_spec_deep_nesting() {
    uint8_t buf[64];
    TLVWriter w(buf, sizeof(buf));
    w.openStruct(kAnon);
      w.openStruct(0);
        w.openArray(0);
          w.putU8(kAnon, 42);
        w.closeContainer();
      w.closeContainer();
    w.closeContainer();
    TEST_ASSERT_FALSE(w.error());

    TLVReader r(buf, w.size());
    r.next(); TEST_ASSERT_TRUE(r.isStruct());   // level 0
    r.next(); TEST_ASSERT_TRUE(r.isStruct());   // level 1
    r.next(); TEST_ASSERT_TRUE(r.isArray());    // level 2
    r.next(); TEST_ASSERT_EQUAL(42, r.getU8());
    r.next(); TEST_ASSERT_TRUE(r.isEnd());      // array end
    r.next(); TEST_ASSERT_TRUE(r.isEnd());      // struct 1 end
    r.next(); TEST_ASSERT_TRUE(r.isEnd());      // struct 0 end
}

// A.12: UInt64 (large value)
void test_tlv_spec_uint64() {
    uint8_t buf[16];
    TLVWriter w(buf, sizeof(buf));
    w.putU64(kAnon, 0xFEDCBA9876543210ULL);
    TEST_ASSERT_EQUAL(9, w.size()); // ctrl(1) + 8 bytes

    TLVReader r(buf, w.size());
    TEST_ASSERT_TRUE(r.next());
    TEST_ASSERT_EQUAL(0xFEDCBA9876543210ULL, r.getU64());
}

// A.13: Empty bytes
void test_tlv_spec_empty_bytes() {
    uint8_t buf[8];
    TLVWriter w(buf, sizeof(buf));
    w.putBytes(kAnon, nullptr, 0);
    TEST_ASSERT_EQUAL(2, w.size()); // ctrl(1) + len(0)

    TLVReader r(buf, w.size());
    TEST_ASSERT_TRUE(r.next());
    size_t len;
    r.getBytes(len);
    TEST_ASSERT_EQUAL(0, len);
}

// A.14: Empty string
void test_tlv_spec_empty_string() {
    uint8_t buf[8];
    TLVWriter w(buf, sizeof(buf));
    w.putString(kAnon, "");
    TEST_ASSERT_EQUAL(2, w.size());

    TLVReader r(buf, w.size());
    TEST_ASSERT_TRUE(r.next());
    TEST_ASSERT_EQUAL(0, r.getLen());
}

// A.15: Skip deeply nested container
void test_tlv_spec_skip_deep_nesting() {
    uint8_t buf[64];
    TLVWriter w(buf, sizeof(buf));
    w.openStruct(kAnon);
      w.openStruct(0);
        w.openArray(0);
          w.putU8(kAnon, 1);
          w.putU8(kAnon, 2);
          w.putU8(kAnon, 3);
        w.closeContainer();
      w.closeContainer();
      w.putU8(1, 99);
    w.closeContainer();

    TLVReader r(buf, w.size());
    r.next(); // outer struct
    r.next(); // inner struct (tag 0)
    r.skipContainer(); // skip entire inner struct

    r.next(); // should land on tag 1 = 99
    TEST_ASSERT_EQUAL(1, r.tag());
    TEST_ASSERT_EQUAL(99, r.getU8());
}

// Matter-specific: AttributePath encoding (used in ReadRequest)
// Path = [endpointId, clusterId, attributeId] as context-tagged in a list
void test_tlv_matter_attribute_path() {
    uint8_t buf[32];
    TLVWriter w(buf, sizeof(buf));
    w.openList(kAnon);
    w.putU16(2, 1);       // tag 2 = endpoint 1
    w.putU32(3, 0x0006);  // tag 3 = cluster OnOff
    w.putU32(4, 0x0000);  // tag 4 = attribute OnOff
    w.closeContainer();
    TEST_ASSERT_FALSE(w.error());

    TLVReader r(buf, w.size());
    r.next(); TEST_ASSERT_TRUE(r.isList());
    r.next(); TEST_ASSERT_EQUAL(2, r.tag()); TEST_ASSERT_EQUAL(1, r.getU16());
    r.next(); TEST_ASSERT_EQUAL(3, r.tag()); TEST_ASSERT_EQUAL(0x0006, r.getU32());
    r.next(); TEST_ASSERT_EQUAL(4, r.tag()); TEST_ASSERT_EQUAL(0x0000, r.getU32());
    r.next(); TEST_ASSERT_TRUE(r.isEnd());
}

// Matter-specific: InvokeRequestIB structure
void test_tlv_matter_invoke_request() {
    uint8_t buf[64];
    TLVWriter w(buf, sizeof(buf));
    // InvokeRequestIB = { SuppressResponse(0), TimedRequest(1), InvokeRequests(2) }
    w.openStruct(kAnon);
      w.putBool(0, false);   // SuppressResponse
      w.putBool(1, false);   // TimedRequest
      w.openArray(2);        // InvokeRequests
        w.openStruct(kAnon); // CommandDataIB
          w.openList(0);     // CommandPath
            w.putU16(0, 1);  // endpoint
            w.putU32(1, 6);  // cluster (OnOff)
            w.putU32(2, 2);  // command (Toggle)
          w.closeContainer();
        w.closeContainer();
      w.closeContainer();
    w.closeContainer();
    TEST_ASSERT_FALSE(w.error());

    // Verify structure parses correctly
    TLVReader r(buf, w.size());
    r.next(); TEST_ASSERT_TRUE(r.isStruct());
    r.next(); TEST_ASSERT_EQUAL(0, r.tag()); TEST_ASSERT_FALSE(r.getBool());
    r.next(); TEST_ASSERT_EQUAL(1, r.tag()); TEST_ASSERT_FALSE(r.getBool());
    r.next(); TEST_ASSERT_EQUAL(2, r.tag()); TEST_ASSERT_TRUE(r.isArray());
    r.next(); TEST_ASSERT_TRUE(r.isStruct()); // CommandDataIB
    r.next(); TEST_ASSERT_EQUAL(0, r.tag()); TEST_ASSERT_TRUE(r.isList()); // CommandPath
    r.next(); TEST_ASSERT_EQUAL(0, r.tag()); TEST_ASSERT_EQUAL(1, r.getU16()); // endpoint
    r.next(); TEST_ASSERT_EQUAL(1, r.tag()); TEST_ASSERT_EQUAL(6, r.getU32()); // cluster
    r.next(); TEST_ASSERT_EQUAL(2, r.tag()); TEST_ASSERT_EQUAL(2, r.getU32()); // command
}

// ============================================================================
// Edge cases
// ============================================================================

void test_tlv_reader_empty_buffer() {
    TLVReader r(nullptr, 0);
    TEST_ASSERT_FALSE(r.next());
}

void test_tlv_reader_truncated_value() {
    // UInt32 needs 4 bytes of value, but we only provide 2
    uint8_t data[] = {kTLVUInt32, 0x01, 0x02};
    TLVReader r(data, sizeof(data));
    // next() may succeed (reads control byte) but pos exceeds size
    r.next();
    // Should flag error or position past end
    TEST_ASSERT_TRUE(r.error() || r.pos() > sizeof(data));
}

void test_tlv_writer_exact_capacity() {
    uint8_t buf[2]; // Exactly enough for anonymous UInt8
    TLVWriter w(buf, sizeof(buf));
    w.putU8(kAnon, 255);
    TEST_ASSERT_FALSE(w.error());
    TEST_ASSERT_EQUAL(2, w.size());
}

void test_tlv_writer_one_byte_short() {
    uint8_t buf[1]; // One byte short for anonymous UInt8
    TLVWriter w(buf, sizeof(buf));
    w.putU8(kAnon, 255);
    TEST_ASSERT_TRUE(w.error());
}

void test_tlv_many_fields_struct() {
    // Struct with 20 fields (tests tag numbers up to 19)
    uint8_t buf[128];
    TLVWriter w(buf, sizeof(buf));
    w.openStruct(kAnon);
    for (uint8_t i = 0; i < 20; i++) {
        w.putU8(i, i * 10);
    }
    w.closeContainer();
    TEST_ASSERT_FALSE(w.error());

    TLVReader r(buf, w.size());
    r.next(); // struct
    for (uint8_t i = 0; i < 20; i++) {
        TEST_ASSERT_TRUE(r.next());
        TEST_ASSERT_EQUAL(i, r.tag());
        TEST_ASSERT_EQUAL(i * 10, r.getU8());
    }
    r.next(); TEST_ASSERT_TRUE(r.isEnd());
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // Encoding
    RUN_TEST(test_tlv_encode_bool_true);
    RUN_TEST(test_tlv_encode_bool_false);
    RUN_TEST(test_tlv_encode_u8);
    RUN_TEST(test_tlv_encode_u16);
    RUN_TEST(test_tlv_encode_u32);
    RUN_TEST(test_tlv_encode_i32_negative);
    RUN_TEST(test_tlv_encode_string);
    RUN_TEST(test_tlv_encode_bytes);
    RUN_TEST(test_tlv_encode_null);
    RUN_TEST(test_tlv_encode_struct);
    RUN_TEST(test_tlv_encode_nested_struct);
    RUN_TEST(test_tlv_encode_array);
    RUN_TEST(test_tlv_encode_overflow);
    RUN_TEST(test_tlv_encode_context_tag);
    RUN_TEST(test_tlv_encode_float);

    // Decoding
    RUN_TEST(test_tlv_decode_u8);
    RUN_TEST(test_tlv_decode_u16);
    RUN_TEST(test_tlv_decode_u32);
    RUN_TEST(test_tlv_decode_bool);
    RUN_TEST(test_tlv_decode_string);
    RUN_TEST(test_tlv_decode_struct);
    RUN_TEST(test_tlv_decode_skip_container);
    RUN_TEST(test_tlv_decode_array);

    // Roundtrip
    RUN_TEST(test_tlv_roundtrip_struct);
    RUN_TEST(test_tlv_roundtrip_nested);

    // Spec Appendix A vectors
    RUN_TEST(test_tlv_spec_signed_integer_1byte);
    RUN_TEST(test_tlv_spec_signed_integer_negative);
    RUN_TEST(test_tlv_spec_empty_struct);
    RUN_TEST(test_tlv_spec_empty_array);
    RUN_TEST(test_tlv_spec_uint8_42);
    RUN_TEST(test_tlv_spec_int16_neg256);
    RUN_TEST(test_tlv_spec_bool_context_tag);
    RUN_TEST(test_tlv_spec_utf8_hello);
    RUN_TEST(test_tlv_spec_octet_string);
    RUN_TEST(test_tlv_spec_null_context);
    RUN_TEST(test_tlv_spec_float32);
    RUN_TEST(test_tlv_spec_struct_mixed_types);
    RUN_TEST(test_tlv_spec_array_of_structs);
    RUN_TEST(test_tlv_spec_list);
    RUN_TEST(test_tlv_spec_deep_nesting);
    RUN_TEST(test_tlv_spec_uint64);
    RUN_TEST(test_tlv_spec_empty_bytes);
    RUN_TEST(test_tlv_spec_empty_string);
    RUN_TEST(test_tlv_spec_skip_deep_nesting);

    // Matter-specific TLV structures
    RUN_TEST(test_tlv_matter_attribute_path);
    RUN_TEST(test_tlv_matter_invoke_request);

    // Edge cases
    RUN_TEST(test_tlv_reader_empty_buffer);
    RUN_TEST(test_tlv_reader_truncated_value);
    RUN_TEST(test_tlv_writer_exact_capacity);
    RUN_TEST(test_tlv_writer_one_byte_short);
    RUN_TEST(test_tlv_many_fields_struct);

    return UNITY_END();
}
