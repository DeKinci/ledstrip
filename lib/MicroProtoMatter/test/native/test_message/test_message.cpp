// Matter Message Header encoding/decoding tests
// Reference: Matter Core Specification, Section 4.4 — Message Format

#include <unity.h>
#include "MatterMessage.h"

using namespace matter;

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// MessageHeader — Encoding
// ============================================================================

void test_msg_header_minimal() {
    // Minimal header: no source, no dest = 8 bytes
    MessageHeader h;
    h.sessionId = 0;
    h.messageCounter = 1;

    uint8_t buf[32];
    size_t len = h.encode(buf, sizeof(buf));
    TEST_ASSERT_EQUAL(8, len);

    // flags byte: no S-bit, no DSIZ
    TEST_ASSERT_EQUAL(0, buf[0] & (kMsgFlagSBit | kMsgFlagDSIZ_Mask));
    // Session ID = 0 (LE)
    TEST_ASSERT_EQUAL(0, buf[2]);
    TEST_ASSERT_EQUAL(0, buf[3]);
    // Message counter = 1 (LE)
    TEST_ASSERT_EQUAL(1, buf[4]);
    TEST_ASSERT_EQUAL(0, buf[5]);
}

void test_msg_header_with_source() {
    MessageHeader h;
    h.hasSrc = true;
    h.sourceNodeId = 0x0102030405060708ULL;
    h.sessionId = 0x1234;
    h.messageCounter = 0xAABBCCDD;

    uint8_t buf[32];
    size_t len = h.encode(buf, sizeof(buf));
    TEST_ASSERT_EQUAL(16, len);  // 8 base + 8 source

    // S-bit set
    TEST_ASSERT_TRUE(buf[0] & kMsgFlagSBit);
    // Session ID LE
    TEST_ASSERT_EQUAL(0x34, buf[2]);
    TEST_ASSERT_EQUAL(0x12, buf[3]);
    // Source node ID starts at offset 8, LE
    TEST_ASSERT_EQUAL(0x08, buf[8]);
    TEST_ASSERT_EQUAL(0x01, buf[15]);
}

void test_msg_header_with_dest() {
    MessageHeader h;
    h.hasDst = true;
    h.destNodeId = 0xFFEEDDCCBBAA9988ULL;
    h.messageCounter = 5;

    uint8_t buf[32];
    size_t len = h.encode(buf, sizeof(buf));
    TEST_ASSERT_EQUAL(16, len);  // 8 base + 8 dest

    TEST_ASSERT_TRUE(buf[0] & kMsgFlagDSIZ_Node);
    TEST_ASSERT_EQUAL(0x88, buf[8]);
    TEST_ASSERT_EQUAL(0xFF, buf[15]);
}

void test_msg_header_with_both() {
    MessageHeader h;
    h.hasSrc = true;
    h.hasDst = true;
    h.sourceNodeId = 1;
    h.destNodeId = 2;
    h.messageCounter = 100;

    uint8_t buf[32];
    size_t len = h.encode(buf, sizeof(buf));
    TEST_ASSERT_EQUAL(24, len);  // 8 + 8 + 8
}

void test_msg_header_buffer_too_small() {
    MessageHeader h;
    h.hasSrc = true;
    h.sourceNodeId = 1;

    uint8_t buf[4];  // Too small
    size_t len = h.encode(buf, sizeof(buf));
    TEST_ASSERT_EQUAL(0, len);
}

// ============================================================================
// MessageHeader — Decoding
// ============================================================================

void test_msg_header_decode_minimal() {
    uint8_t data[] = {0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};
    MessageHeader h;
    size_t len = h.decode(data, sizeof(data));
    TEST_ASSERT_EQUAL(8, len);
    TEST_ASSERT_FALSE(h.hasSrc);
    TEST_ASSERT_FALSE(h.hasDst);
    TEST_ASSERT_EQUAL(0, h.sessionId);
    TEST_ASSERT_EQUAL(1, h.messageCounter);
}

void test_msg_header_decode_with_source() {
    // Build a header with source
    MessageHeader orig;
    orig.hasSrc = true;
    orig.sourceNodeId = 0x123456789ABCDEF0ULL;
    orig.sessionId = 42;
    orig.messageCounter = 999;

    uint8_t buf[32];
    size_t encLen = orig.encode(buf, sizeof(buf));

    MessageHeader decoded;
    size_t decLen = decoded.decode(buf, encLen);
    TEST_ASSERT_EQUAL(encLen, decLen);
    TEST_ASSERT_TRUE(decoded.hasSrc);
    TEST_ASSERT_EQUAL(orig.sourceNodeId, decoded.sourceNodeId);
    TEST_ASSERT_EQUAL(42, decoded.sessionId);
    TEST_ASSERT_EQUAL(999, decoded.messageCounter);
}

void test_msg_header_roundtrip() {
    MessageHeader orig;
    orig.hasSrc = true;
    orig.hasDst = true;
    orig.sourceNodeId = 0x1111111111111111ULL;
    orig.destNodeId = 0x2222222222222222ULL;
    orig.sessionId = 0xABCD;
    orig.messageCounter = 0x12345678;
    orig.securityFlags = 0x01;

    uint8_t buf[32];
    size_t encLen = orig.encode(buf, sizeof(buf));

    MessageHeader decoded;
    size_t decLen = decoded.decode(buf, encLen);
    TEST_ASSERT_EQUAL(encLen, decLen);
    TEST_ASSERT_TRUE(decoded.hasSrc);
    TEST_ASSERT_TRUE(decoded.hasDst);
    TEST_ASSERT_EQUAL(orig.sourceNodeId, decoded.sourceNodeId);
    TEST_ASSERT_EQUAL(orig.destNodeId, decoded.destNodeId);
    TEST_ASSERT_EQUAL(0xABCD, decoded.sessionId);
    TEST_ASSERT_EQUAL(0x12345678, decoded.messageCounter);
    TEST_ASSERT_EQUAL(0x01, decoded.securityFlags);
}

void test_msg_header_decode_too_short() {
    uint8_t data[] = {0x00, 0x00, 0x00};
    MessageHeader h;
    TEST_ASSERT_EQUAL(0, h.decode(data, sizeof(data)));
}

// ============================================================================
// ProtocolHeader — Encoding/Decoding
// ============================================================================

void test_proto_header_minimal() {
    ProtocolHeader ph;
    ph.opcode = 0x20;  // PBKDFParamRequest
    ph.exchangeId = 1;
    ph.protocolId = kProtoSecureChannel;
    ph.isInitiator = true;
    ph.needsAck = true;

    uint8_t buf[16];
    size_t len = ph.encode(buf, sizeof(buf));
    TEST_ASSERT_EQUAL(6, len);  // No ACK field

    TEST_ASSERT_TRUE(buf[0] & kExInitiator);
    TEST_ASSERT_TRUE(buf[0] & kExReliable);
    TEST_ASSERT_FALSE(buf[0] & kExAck);
    TEST_ASSERT_EQUAL(0x20, buf[1]);
}

void test_proto_header_with_ack() {
    ProtocolHeader ph;
    ph.opcode = 0x05;
    ph.exchangeId = 100;
    ph.protocolId = kProtoInteractionModel;
    ph.hasAck = true;
    ph.ackCounter = 0xDEAD;

    uint8_t buf[16];
    size_t len = ph.encode(buf, sizeof(buf));
    TEST_ASSERT_EQUAL(10, len);  // 6 base + 4 ack
    TEST_ASSERT_TRUE(buf[0] & kExAck);
}

void test_proto_header_roundtrip() {
    ProtocolHeader orig;
    orig.opcode = 0x08;  // InvokeRequest
    orig.exchangeId = 42;
    orig.protocolId = kProtoInteractionModel;
    orig.isInitiator = true;
    orig.needsAck = true;
    orig.hasAck = true;
    orig.ackCounter = 12345;

    uint8_t buf[16];
    size_t encLen = orig.encode(buf, sizeof(buf));

    ProtocolHeader decoded;
    size_t decLen = decoded.decode(buf, encLen);
    TEST_ASSERT_EQUAL(encLen, decLen);
    TEST_ASSERT_EQUAL(0x08, decoded.opcode);
    TEST_ASSERT_EQUAL(42, decoded.exchangeId);
    TEST_ASSERT_EQUAL(kProtoInteractionModel, decoded.protocolId);
    TEST_ASSERT_TRUE(decoded.isInitiator);
    TEST_ASSERT_TRUE(decoded.needsAck);
    TEST_ASSERT_TRUE(decoded.hasAck);
    TEST_ASSERT_EQUAL(12345, decoded.ackCounter);
}

// ============================================================================
// StatusReport
// ============================================================================

void test_status_report_roundtrip() {
    StatusReport orig;
    orig.generalCode = 0;  // SUCCESS
    orig.protocolId = kProtoSecureChannel;
    orig.protocolCode = 0;

    uint8_t buf[16];
    size_t encLen = orig.encode(buf, sizeof(buf));
    TEST_ASSERT_EQUAL(8, encLen);

    StatusReport decoded;
    size_t decLen = decoded.decode(buf, encLen);
    TEST_ASSERT_EQUAL(8, decLen);
    TEST_ASSERT_EQUAL(0, decoded.generalCode);
    TEST_ASSERT_EQUAL(kProtoSecureChannel, decoded.protocolId);
    TEST_ASSERT_EQUAL(0, decoded.protocolCode);
}

void test_status_report_failure() {
    StatusReport sr;
    sr.generalCode = 1;  // FAILURE
    sr.protocolId = kProtoInteractionModel;
    sr.protocolCode = 0x0580;  // InvalidAction

    uint8_t buf[16];
    size_t len = sr.encode(buf, sizeof(buf));

    StatusReport decoded;
    decoded.decode(buf, len);
    TEST_ASSERT_EQUAL(1, decoded.generalCode);
    TEST_ASSERT_EQUAL(kProtoInteractionModel, decoded.protocolId);
    TEST_ASSERT_EQUAL(0x0580, decoded.protocolCode);
}

// ============================================================================
// Security Flags (Matter Spec 4.4.1.2)
// ============================================================================

void test_msg_security_flags_encode() {
    MessageHeader h;
    h.securityFlags = kSecFlagPrivacy | kSecFlagControl;
    h.messageCounter = 1;

    uint8_t buf[16];
    size_t len = h.encode(buf, sizeof(buf));
    TEST_ASSERT_EQUAL(kSecFlagPrivacy | kSecFlagControl, buf[1]);
}

void test_msg_security_flags_decode() {
    uint8_t data[] = {0x00, kSecFlagControl | 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};
    MessageHeader h;
    h.decode(data, sizeof(data));
    TEST_ASSERT_TRUE(h.securityFlags & kSecFlagControl);
    TEST_ASSERT_EQUAL(0x01, h.securityFlags & kSecFlagSessionMask);
}

// ============================================================================
// Nonce construction (Matter Spec 4.5.1.1)
// Nonce = SecurityFlags(1) || MessageCounter(4, LE) || SourceNodeId(8, LE)
// ============================================================================

void test_nonce_construction() {
    // Per spec: nonce is 13 bytes
    uint8_t secFlags = 0x00;
    uint32_t msgCounter = 0x01020304;
    uint64_t sourceNode = 0x0102030405060708ULL;

    uint8_t nonce[13];
    nonce[0] = secFlags;
    nonce[1] = msgCounter & 0xFF;
    nonce[2] = (msgCounter >> 8) & 0xFF;
    nonce[3] = (msgCounter >> 16) & 0xFF;
    nonce[4] = (msgCounter >> 24) & 0xFF;
    for (int i = 0; i < 8; i++) nonce[5 + i] = (sourceNode >> (i * 8)) & 0xFF;

    TEST_ASSERT_EQUAL(0x00, nonce[0]);
    TEST_ASSERT_EQUAL(0x04, nonce[1]); // counter LE
    TEST_ASSERT_EQUAL(0x03, nonce[2]);
    TEST_ASSERT_EQUAL(0x02, nonce[3]);
    TEST_ASSERT_EQUAL(0x01, nonce[4]);
    TEST_ASSERT_EQUAL(0x08, nonce[5]); // node ID LE
    TEST_ASSERT_EQUAL(0x01, nonce[12]);
}

// ============================================================================
// Protocol-specific message formats
// ============================================================================

// PBKDFParamRequest (Secure Channel 0x20) — per spec 4.13.1.1
void test_pbkdf_param_request_format() {
    ProtocolHeader ph;
    ph.opcode = kOpPBKDFParamRequest;
    ph.exchangeId = 1;
    ph.protocolId = kProtoSecureChannel;
    ph.isInitiator = true;
    ph.needsAck = true;

    uint8_t buf[16];
    size_t len = ph.encode(buf, sizeof(buf));
    TEST_ASSERT_EQUAL(6, len);
    TEST_ASSERT_EQUAL(kOpPBKDFParamRequest, buf[1]);
    TEST_ASSERT_EQUAL(kProtoSecureChannel, buf[4] | (buf[5] << 8));
}

// ReadRequest (IM 0x02) — per spec 8.4.2
void test_read_request_format() {
    ProtocolHeader ph;
    ph.opcode = kOpReadRequest;
    ph.protocolId = kProtoInteractionModel;
    ph.exchangeId = 42;
    ph.isInitiator = true;
    ph.needsAck = true;

    uint8_t buf[16];
    size_t len = ph.encode(buf, sizeof(buf));
    TEST_ASSERT_EQUAL(kOpReadRequest, buf[1]);
    TEST_ASSERT_EQUAL(42, buf[2] | (buf[3] << 8));
    TEST_ASSERT_EQUAL(kProtoInteractionModel, buf[4] | (buf[5] << 8));
}

// InvokeRequest (IM 0x08)
void test_invoke_request_format() {
    ProtocolHeader ph;
    ph.opcode = kOpInvokeRequest;
    ph.protocolId = kProtoInteractionModel;
    ph.exchangeId = 100;
    ph.isInitiator = true;
    ph.needsAck = true;
    ph.hasAck = true;
    ph.ackCounter = 0xAAAA;

    uint8_t buf[16];
    size_t len = ph.encode(buf, sizeof(buf));
    TEST_ASSERT_EQUAL(10, len); // 6 + 4 ack

    ProtocolHeader decoded;
    decoded.decode(buf, len);
    TEST_ASSERT_EQUAL(kOpInvokeRequest, decoded.opcode);
    TEST_ASSERT_EQUAL(100, decoded.exchangeId);
    TEST_ASSERT_EQUAL(0xAAAA, decoded.ackCounter);
}

// CASE Sigma1 (0x30)
void test_case_sigma1_format() {
    ProtocolHeader ph;
    ph.opcode = kOpCaseSigma1;
    ph.protocolId = kProtoSecureChannel;
    ph.exchangeId = 1;
    ph.isInitiator = true;
    ph.needsAck = true;

    uint8_t buf[16];
    ph.encode(buf, sizeof(buf));
    TEST_ASSERT_EQUAL(kOpCaseSigma1, buf[1]);
    TEST_ASSERT_EQUAL(kProtoSecureChannel, buf[4] | (buf[5] << 8));
}

// StatusReport for session establishment success
void test_status_report_session_established() {
    StatusReport sr;
    sr.generalCode = 0; // SUCCESS
    sr.protocolId = kProtoSecureChannel;
    sr.protocolCode = kProtoCodeSessionEstablished;

    uint8_t buf[16];
    size_t len = sr.encode(buf, sizeof(buf));

    StatusReport decoded;
    decoded.decode(buf, len);
    TEST_ASSERT_EQUAL(0, decoded.generalCode);
    TEST_ASSERT_EQUAL(kProtoSecureChannel, decoded.protocolId);
    TEST_ASSERT_EQUAL(kProtoCodeSessionEstablished, decoded.protocolCode);
}

// StatusReport for CASE no shared root cert
void test_status_report_no_shared_root() {
    StatusReport sr;
    sr.generalCode = 1; // FAILURE
    sr.protocolId = kProtoSecureChannel;
    sr.protocolCode = kProtoCodeNoSharedRoot;

    uint8_t buf[16];
    sr.encode(buf, sizeof(buf));

    StatusReport decoded;
    decoded.decode(buf, sizeof(buf));
    TEST_ASSERT_EQUAL(1, decoded.generalCode);
    TEST_ASSERT_EQUAL(kProtoCodeNoSharedRoot, decoded.protocolCode);
}

// ============================================================================
// Message Counter (Spec 4.5.4)
// ============================================================================

void test_msg_counter_wrap() {
    // Counter should encode max uint32 correctly
    MessageHeader h;
    h.messageCounter = 0xFFFFFFFF;
    uint8_t buf[16];
    h.encode(buf, sizeof(buf));

    MessageHeader decoded;
    decoded.decode(buf, 8);
    TEST_ASSERT_EQUAL(0xFFFFFFFF, decoded.messageCounter);
}

void test_msg_counter_zero() {
    MessageHeader h;
    h.messageCounter = 0;
    uint8_t buf[16];
    h.encode(buf, sizeof(buf));

    MessageHeader decoded;
    decoded.decode(buf, 8);
    TEST_ASSERT_EQUAL(0, decoded.messageCounter);
}

// ============================================================================
// Session ID (Spec 4.4.1.3)
// ============================================================================

void test_session_id_max() {
    MessageHeader h;
    h.sessionId = 0xFFFF;
    h.messageCounter = 1;
    uint8_t buf[16];
    h.encode(buf, sizeof(buf));

    MessageHeader decoded;
    decoded.decode(buf, 8);
    TEST_ASSERT_EQUAL(0xFFFF, decoded.sessionId);
}

void test_session_id_unsecured() {
    // Session 0 = unsecured (used for PASE/CASE handshake)
    MessageHeader h;
    h.sessionId = 0;
    uint8_t buf[16];
    h.encode(buf, sizeof(buf));
    TEST_ASSERT_EQUAL(0, buf[2]);
    TEST_ASSERT_EQUAL(0, buf[3]);
}

// ============================================================================
// Exchange Flags (Spec 4.4.3)
// ============================================================================

void test_exchange_flags_all_set() {
    ProtocolHeader ph;
    ph.isInitiator = true;
    ph.needsAck = true;
    ph.hasAck = true;
    ph.ackCounter = 123;
    ph.opcode = 0x01;
    ph.exchangeId = 1;
    ph.protocolId = 0;

    uint8_t buf[16];
    ph.encode(buf, sizeof(buf));
    TEST_ASSERT_TRUE(buf[0] & kExInitiator);
    TEST_ASSERT_TRUE(buf[0] & kExReliable);
    TEST_ASSERT_TRUE(buf[0] & kExAck);
}

void test_exchange_flags_none_set() {
    ProtocolHeader ph;
    ph.isInitiator = false;
    ph.needsAck = false;
    ph.hasAck = false;
    ph.opcode = 0x01;
    ph.exchangeId = 1;
    ph.protocolId = 0;

    uint8_t buf[16];
    ph.encode(buf, sizeof(buf));
    TEST_ASSERT_EQUAL(0, buf[0] & (kExInitiator | kExReliable | kExAck));
}

// ============================================================================
// Encoded Size (for AAD computation)
// ============================================================================

void test_msg_encoded_size() {
    MessageHeader h;
    h.hasSrc = false;
    h.hasDst = false;
    TEST_ASSERT_EQUAL(8, h.encodedSize());

    h.hasSrc = true;
    TEST_ASSERT_EQUAL(16, h.encodedSize());

    h.hasDst = true;
    TEST_ASSERT_EQUAL(24, h.encodedSize());

    h.hasSrc = false;
    TEST_ASSERT_EQUAL(16, h.encodedSize());
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // MessageHeader — basic
    RUN_TEST(test_msg_header_minimal);
    RUN_TEST(test_msg_header_with_source);
    RUN_TEST(test_msg_header_with_dest);
    RUN_TEST(test_msg_header_with_both);
    RUN_TEST(test_msg_header_buffer_too_small);
    RUN_TEST(test_msg_header_decode_minimal);
    RUN_TEST(test_msg_header_decode_with_source);
    RUN_TEST(test_msg_header_roundtrip);
    RUN_TEST(test_msg_header_decode_too_short);

    // Security flags
    RUN_TEST(test_msg_security_flags_encode);
    RUN_TEST(test_msg_security_flags_decode);

    // Nonce construction
    RUN_TEST(test_nonce_construction);

    // Message counter
    RUN_TEST(test_msg_counter_wrap);
    RUN_TEST(test_msg_counter_zero);

    // Session ID
    RUN_TEST(test_session_id_max);
    RUN_TEST(test_session_id_unsecured);

    // Encoded size (AAD)
    RUN_TEST(test_msg_encoded_size);

    // ProtocolHeader
    RUN_TEST(test_proto_header_minimal);
    RUN_TEST(test_proto_header_with_ack);
    RUN_TEST(test_proto_header_roundtrip);

    // Exchange flags
    RUN_TEST(test_exchange_flags_all_set);
    RUN_TEST(test_exchange_flags_none_set);

    // Protocol-specific formats
    RUN_TEST(test_pbkdf_param_request_format);
    RUN_TEST(test_read_request_format);
    RUN_TEST(test_invoke_request_format);
    RUN_TEST(test_case_sigma1_format);

    // StatusReport
    RUN_TEST(test_status_report_roundtrip);
    RUN_TEST(test_status_report_failure);
    RUN_TEST(test_status_report_session_established);
    RUN_TEST(test_status_report_no_shared_root);

    return UNITY_END();
}
