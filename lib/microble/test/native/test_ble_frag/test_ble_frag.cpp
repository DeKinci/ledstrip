#ifdef NATIVE_TEST

#define MICROBLE_MAX_MSG_SIZE 1024

#include <unity.h>
#include <BleFragmentation.h>
#include <cstring>
#include <vector>

using namespace MicroBLE;

// =========== bleFragmentSend Tests ===========

void test_frag_complete_message_fits_mtu() {
    // Message fits in one notification — should get COMPLETE header
    uint8_t msg[] = {0x01, 0x02, 0x03};
    std::vector<std::vector<uint8_t>> fragments;

    size_t count = bleFragmentSend(msg, sizeof(msg), 20, [&](const uint8_t* data, size_t len) {
        fragments.push_back(std::vector<uint8_t>(data, data + len));
    });

    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_EQUAL(1, fragments.size());
    TEST_ASSERT_EQUAL(4, fragments[0].size());  // 1 header + 3 data
    TEST_ASSERT_EQUAL_HEX8(BleFragHeader::COMPLETE, fragments[0][0]);
    TEST_ASSERT_EQUAL(0x01, fragments[0][1]);
    TEST_ASSERT_EQUAL(0x02, fragments[0][2]);
    TEST_ASSERT_EQUAL(0x03, fragments[0][3]);
}

void test_frag_message_splits_into_two() {
    // 10 bytes of data, MTU payload = 6 → chunk size = 5
    // Fragment 1: START + 5 bytes, Fragment 2: END + 5 bytes
    uint8_t msg[10];
    for (int i = 0; i < 10; i++) msg[i] = i;

    std::vector<std::vector<uint8_t>> fragments;

    size_t count = bleFragmentSend(msg, sizeof(msg), 6, [&](const uint8_t* data, size_t len) {
        fragments.push_back(std::vector<uint8_t>(data, data + len));
    });

    TEST_ASSERT_EQUAL(2, count);
    TEST_ASSERT_EQUAL(2, fragments.size());

    // First fragment: START
    TEST_ASSERT_EQUAL(6, fragments[0].size());
    TEST_ASSERT_EQUAL_HEX8(BleFragHeader::START, fragments[0][0]);
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL(i, fragments[0][i + 1]);
    }

    // Last fragment: END
    TEST_ASSERT_EQUAL(6, fragments[1].size());
    TEST_ASSERT_EQUAL_HEX8(BleFragHeader::END, fragments[1][0]);
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL(i + 5, fragments[1][i + 1]);
    }
}

void test_frag_message_splits_into_three() {
    // 12 bytes of data, MTU payload = 6 → chunk size = 5
    uint8_t msg[12];
    for (int i = 0; i < 12; i++) msg[i] = i;

    std::vector<std::vector<uint8_t>> fragments;

    size_t count = bleFragmentSend(msg, sizeof(msg), 6, [&](const uint8_t* data, size_t len) {
        fragments.push_back(std::vector<uint8_t>(data, data + len));
    });

    TEST_ASSERT_EQUAL(3, count);
    TEST_ASSERT_EQUAL_HEX8(BleFragHeader::START, fragments[0][0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, fragments[1][0]);   // Middle
    TEST_ASSERT_EQUAL_HEX8(BleFragHeader::END, fragments[2][0]);
    TEST_ASSERT_EQUAL(3, fragments[2].size());  // 1 header + 2 remaining
}

void test_frag_exactly_mtu_no_split() {
    uint8_t msg[5] = {1, 2, 3, 4, 5};
    std::vector<std::vector<uint8_t>> fragments;

    size_t count = bleFragmentSend(msg, sizeof(msg), 6, [&](const uint8_t* data, size_t len) {
        fragments.push_back(std::vector<uint8_t>(data, data + len));
    });

    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_EQUAL_HEX8(BleFragHeader::COMPLETE, fragments[0][0]);
}

void test_frag_one_byte_over_mtu() {
    uint8_t msg[6] = {1, 2, 3, 4, 5, 6};
    std::vector<std::vector<uint8_t>> fragments;

    size_t count = bleFragmentSend(msg, sizeof(msg), 6, [&](const uint8_t* data, size_t len) {
        fragments.push_back(std::vector<uint8_t>(data, data + len));
    });

    TEST_ASSERT_EQUAL(2, count);
    TEST_ASSERT_EQUAL_HEX8(BleFragHeader::START, fragments[0][0]);
    TEST_ASSERT_EQUAL_HEX8(BleFragHeader::END, fragments[1][0]);
    TEST_ASSERT_EQUAL(2, fragments[1].size());
}

void test_frag_single_byte_message() {
    uint8_t msg[] = {0xFF};
    std::vector<std::vector<uint8_t>> fragments;

    size_t count = bleFragmentSend(msg, 1, 20, [&](const uint8_t* data, size_t len) {
        fragments.push_back(std::vector<uint8_t>(data, data + len));
    });

    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_EQUAL(2, fragments[0].size());
    TEST_ASSERT_EQUAL_HEX8(BleFragHeader::COMPLETE, fragments[0][0]);
    TEST_ASSERT_EQUAL(0xFF, fragments[0][1]);
}

void test_frag_mtu_too_small() {
    uint8_t msg[] = {1};
    size_t count = bleFragmentSend(msg, 1, 1, [&](const uint8_t*, size_t) {});
    TEST_ASSERT_EQUAL(0, count);
}

void test_frag_empty_message() {
    std::vector<std::vector<uint8_t>> fragments;
    size_t count = bleFragmentSend(nullptr, 0, 20, [&](const uint8_t* data, size_t len) {
        fragments.push_back(std::vector<uint8_t>(data, data + len));
    });
    // Empty message: COMPLETE header + 0 data bytes
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_EQUAL(1, fragments[0].size());
    TEST_ASSERT_EQUAL_HEX8(BleFragHeader::COMPLETE, fragments[0][0]);
}

// =========== BleReassembler Tests ===========

void test_reasm_complete_message() {
    BleReassembler r;
    uint8_t frag[] = {BleFragHeader::COMPLETE, 0x01, 0x02, 0x03};
    TEST_ASSERT_TRUE(r.feed(frag, sizeof(frag)));
    TEST_ASSERT_EQUAL(3, r.length());
    TEST_ASSERT_EQUAL_MEMORY("\x01\x02\x03", r.buffer(), 3);
}

void test_reasm_two_fragments() {
    BleReassembler r;
    uint8_t frag1[] = {BleFragHeader::START, 0x01, 0x02};
    uint8_t frag2[] = {BleFragHeader::END, 0x03, 0x04};
    TEST_ASSERT_FALSE(r.feed(frag1, sizeof(frag1)));
    TEST_ASSERT_TRUE(r.feed(frag2, sizeof(frag2)));
    TEST_ASSERT_EQUAL(4, r.length());
}

void test_reasm_three_fragments() {
    BleReassembler r;
    uint8_t frag1[] = {BleFragHeader::START, 0xAA};
    uint8_t frag2[] = {0x00, 0xBB};  // Middle
    uint8_t frag3[] = {BleFragHeader::END, 0xCC};
    TEST_ASSERT_FALSE(r.feed(frag1, sizeof(frag1)));
    TEST_ASSERT_FALSE(r.feed(frag2, sizeof(frag2)));
    TEST_ASSERT_TRUE(r.feed(frag3, sizeof(frag3)));
    TEST_ASSERT_EQUAL(3, r.length());
}

void test_reasm_start_resets_previous() {
    BleReassembler r;
    uint8_t frag1[] = {BleFragHeader::START, 0x01};
    uint8_t frag2[] = {BleFragHeader::START, 0x02};  // New start
    uint8_t frag3[] = {BleFragHeader::END, 0x03};
    TEST_ASSERT_FALSE(r.feed(frag1, sizeof(frag1)));
    TEST_ASSERT_FALSE(r.feed(frag2, sizeof(frag2)));
    TEST_ASSERT_TRUE(r.feed(frag3, sizeof(frag3)));
    TEST_ASSERT_EQUAL(2, r.length());
    TEST_ASSERT_EQUAL(0x02, r.buffer()[0]);
}

void test_reasm_middle_without_start_discarded() {
    BleReassembler r;
    uint8_t frag[] = {0x00, 0x01, 0x02};
    TEST_ASSERT_FALSE(r.feed(frag, sizeof(frag)));
    uint8_t end[] = {BleFragHeader::END, 0x03};
    TEST_ASSERT_FALSE(r.feed(end, sizeof(end)));
}

void test_reasm_overflow_discards() {
    // MICROBLE_MAX_MSG_SIZE is 1024 — send fragments that exceed it
    BleReassembler r;
    // First fragment: 512 bytes of payload
    uint8_t frag1[1 + 512];
    frag1[0] = BleFragHeader::START;
    memset(frag1 + 1, 0xAA, 512);
    TEST_ASSERT_FALSE(r.feed(frag1, sizeof(frag1)));

    // Second fragment: another 512 bytes
    uint8_t frag2[1 + 512];
    frag2[0] = 0x00;  // middle
    memset(frag2 + 1, 0xBB, 512);
    TEST_ASSERT_FALSE(r.feed(frag2, sizeof(frag2)));

    // Third fragment: 1 more byte would exceed 1024 — overflow
    uint8_t frag3[] = {BleFragHeader::END, 0xCC};
    TEST_ASSERT_FALSE(r.feed(frag3, sizeof(frag3)));
}

void test_reasm_empty_fragment() {
    BleReassembler r;
    uint8_t empty[] = {};
    TEST_ASSERT_FALSE(r.feed(empty, 0));
}

void test_reasm_reset_allows_reuse() {
    BleReassembler r;
    uint8_t frag[] = {BleFragHeader::COMPLETE, 0xAA};
    TEST_ASSERT_TRUE(r.feed(frag, sizeof(frag)));
    TEST_ASSERT_EQUAL(1, r.length());

    r.reset();
    TEST_ASSERT_EQUAL(0, r.length());

    uint8_t frag2[] = {BleFragHeader::COMPLETE, 0xBB};
    TEST_ASSERT_TRUE(r.feed(frag2, sizeof(frag2)));
    TEST_ASSERT_EQUAL(1, r.length());
    TEST_ASSERT_EQUAL(0xBB, r.buffer()[0]);
}

void test_reasm_sequential_messages() {
    // Feed two complete messages back-to-back (with reset between)
    BleReassembler r;

    uint8_t msg1[] = {BleFragHeader::COMPLETE, 0x01, 0x02};
    TEST_ASSERT_TRUE(r.feed(msg1, sizeof(msg1)));
    TEST_ASSERT_EQUAL(2, r.length());
    r.reset();

    uint8_t msg2[] = {BleFragHeader::COMPLETE, 0x03, 0x04, 0x05};
    TEST_ASSERT_TRUE(r.feed(msg2, sizeof(msg2)));
    TEST_ASSERT_EQUAL(3, r.length());
    TEST_ASSERT_EQUAL(0x03, r.buffer()[0]);
}

void test_reasm_header_only_complete() {
    // COMPLETE with zero-length payload
    BleReassembler r;
    uint8_t frag[] = {BleFragHeader::COMPLETE};
    TEST_ASSERT_TRUE(r.feed(frag, 1));
    TEST_ASSERT_EQUAL(0, r.length());
}

// =========== Roundtrip: fragment then reassemble ===========

void test_frag_roundtrip_small() {
    uint8_t original[] = {0xDE, 0xAD, 0xBE, 0xEF};
    BleReassembler r;

    bleFragmentSend(original, sizeof(original), 20, [&](const uint8_t* data, size_t len) {
        r.feed(data, len);
    });

    TEST_ASSERT_EQUAL(sizeof(original), r.length());
    TEST_ASSERT_EQUAL_MEMORY(original, r.buffer(), sizeof(original));
}

void test_frag_roundtrip_large() {
    uint8_t original[100];
    for (int i = 0; i < 100; i++) original[i] = i;

    BleReassembler r;
    size_t fragCount = bleFragmentSend(original, sizeof(original), 10, [&](const uint8_t* data, size_t len) {
        r.feed(data, len);
    });

    TEST_ASSERT_GREATER_THAN(1, fragCount);
    TEST_ASSERT_EQUAL(sizeof(original), r.length());
    TEST_ASSERT_EQUAL_MEMORY(original, r.buffer(), sizeof(original));
}

void test_frag_roundtrip_realistic_mtu() {
    uint8_t original[200];
    for (int i = 0; i < 200; i++) original[i] = i & 0xFF;

    BleReassembler r;
    size_t fragCount = bleFragmentSend(original, sizeof(original), 244, [&](const uint8_t* data, size_t len) {
        r.feed(data, len);
    });

    TEST_ASSERT_EQUAL(1, fragCount);
    TEST_ASSERT_EQUAL(sizeof(original), r.length());
    TEST_ASSERT_EQUAL_MEMORY(original, r.buffer(), sizeof(original));
}

void test_frag_roundtrip_exceeds_mtu() {
    uint8_t original[500];
    for (int i = 0; i < 500; i++) original[i] = i & 0xFF;

    BleReassembler r;
    size_t fragCount = bleFragmentSend(original, sizeof(original), 244, [&](const uint8_t* data, size_t len) {
        r.feed(data, len);
    });

    TEST_ASSERT_EQUAL(3, fragCount);
    TEST_ASSERT_EQUAL(sizeof(original), r.length());
    TEST_ASSERT_EQUAL_MEMORY(original, r.buffer(), sizeof(original));
}

void test_frag_roundtrip_min_mtu() {
    // Minimum useful MTU: payload = 2 (header + 1 byte per fragment)
    uint8_t original[10];
    for (int i = 0; i < 10; i++) original[i] = i;

    BleReassembler r;
    size_t fragCount = bleFragmentSend(original, sizeof(original), 2, [&](const uint8_t* data, size_t len) {
        r.feed(data, len);
    });

    TEST_ASSERT_EQUAL(10, fragCount);  // 1 byte per chunk
    TEST_ASSERT_EQUAL(sizeof(original), r.length());
    TEST_ASSERT_EQUAL_MEMORY(original, r.buffer(), sizeof(original));
}

// =========== Test Runner ===========

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // Fragmentation (TX)
    RUN_TEST(test_frag_complete_message_fits_mtu);
    RUN_TEST(test_frag_message_splits_into_two);
    RUN_TEST(test_frag_message_splits_into_three);
    RUN_TEST(test_frag_exactly_mtu_no_split);
    RUN_TEST(test_frag_one_byte_over_mtu);
    RUN_TEST(test_frag_single_byte_message);
    RUN_TEST(test_frag_mtu_too_small);
    RUN_TEST(test_frag_empty_message);

    // Reassembly (RX)
    RUN_TEST(test_reasm_complete_message);
    RUN_TEST(test_reasm_two_fragments);
    RUN_TEST(test_reasm_three_fragments);
    RUN_TEST(test_reasm_start_resets_previous);
    RUN_TEST(test_reasm_middle_without_start_discarded);
    RUN_TEST(test_reasm_overflow_discards);
    RUN_TEST(test_reasm_empty_fragment);
    RUN_TEST(test_reasm_reset_allows_reuse);
    RUN_TEST(test_reasm_sequential_messages);
    RUN_TEST(test_reasm_header_only_complete);

    // Roundtrip
    RUN_TEST(test_frag_roundtrip_small);
    RUN_TEST(test_frag_roundtrip_large);
    RUN_TEST(test_frag_roundtrip_realistic_mtu);
    RUN_TEST(test_frag_roundtrip_exceeds_mtu);
    RUN_TEST(test_frag_roundtrip_min_mtu);

    return UNITY_END();
}

#endif // NATIVE_TEST
