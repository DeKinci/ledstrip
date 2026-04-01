// Matter MRP (Message Reliability Protocol) tests
// Reference: Matter Spec Section 4.11 — Message Reliability Protocol

#include <unity.h>
#include "MatterMRP.h"

using namespace matter;

static MRP mrp;

void setUp(void) { mrp.reset(); }
void tearDown(void) {}

// ============================================================================
// Send Counter (Spec 4.5.4)
// ============================================================================

void test_send_counter_increments() {
    mrp.initCounter(100);
    uint32_t c1 = mrp.nextSendCounter();
    uint32_t c2 = mrp.nextSendCounter();
    uint32_t c3 = mrp.nextSendCounter();
    TEST_ASSERT_EQUAL(c1 + 1, c2);
    TEST_ASSERT_EQUAL(c2 + 1, c3);
}

void test_send_counter_init_range() {
    // Spec: initialize to random in [1, 2^28]
    mrp.initCounter(0);
    TEST_ASSERT_EQUAL(1, mrp.nextSendCounter()); // 0 % 0x0FFFFFFF + 1 = 1

    mrp.initCounter(0x0FFFFFFF);
    uint32_t c = mrp.nextSendCounter();
    TEST_ASSERT_TRUE(c >= 1 && c <= 0x0FFFFFFF);
}

void test_send_counter_sequential() {
    mrp.initCounter(1000);
    uint32_t first = mrp.nextSendCounter();
    for (int i = 0; i < 100; i++) {
        uint32_t next = mrp.nextSendCounter();
        TEST_ASSERT_EQUAL(first + i + 1, next);
    }
}

// ============================================================================
// Deduplication — 32-entry sliding window (Spec 4.6.5.1)
// ============================================================================

void test_dedup_first_message_not_duplicate() {
    TEST_ASSERT_FALSE(mrp.isDuplicate(100));
}

void test_dedup_after_receive_same_is_duplicate() {
    mrp.onReceived(100, false);
    TEST_ASSERT_TRUE(mrp.isDuplicate(100));
}

void test_dedup_new_counter_not_duplicate() {
    mrp.onReceived(100, false);
    TEST_ASSERT_FALSE(mrp.isDuplicate(101));
}

void test_dedup_old_in_window() {
    mrp.onReceived(100, false);
    mrp.onReceived(105, false);
    // Counter 100 is 5 behind max (105), within 32-bit window
    TEST_ASSERT_TRUE(mrp.isDuplicate(100));
}

void test_dedup_old_outside_window() {
    mrp.onReceived(100, false);
    mrp.onReceived(200, false); // 100 difference — outside 32-entry window
    TEST_ASSERT_TRUE(mrp.isDuplicate(100)); // Too old = treated as duplicate
}

void test_dedup_sequential_messages() {
    for (uint32_t i = 1; i <= 10; i++) {
        TEST_ASSERT_FALSE(mrp.isDuplicate(i));
        mrp.onReceived(i, false);
        TEST_ASSERT_TRUE(mrp.isDuplicate(i));
    }
}

void test_dedup_out_of_order_within_window() {
    mrp.onReceived(100, false);
    mrp.onReceived(102, false); // skip 101
    mrp.onReceived(101, false); // late arrival
    TEST_ASSERT_TRUE(mrp.isDuplicate(101)); // Now marked
    TEST_ASSERT_TRUE(mrp.isDuplicate(102));
    TEST_ASSERT_TRUE(mrp.isDuplicate(100));
}

void test_dedup_window_boundary_31() {
    mrp.onReceived(0, false);
    mrp.onReceived(31, false); // Exactly 31 behind = last slot in 32-bit window
    TEST_ASSERT_TRUE(mrp.isDuplicate(0));
}

void test_dedup_window_boundary_32() {
    mrp.onReceived(0, false);
    mrp.onReceived(32, false); // 32 behind = outside window
    TEST_ASSERT_TRUE(mrp.isDuplicate(0)); // Too old
}

// ============================================================================
// ACK Tracking (Spec 4.11.2.4)
// ============================================================================

void test_ack_not_pending_initially() {
    TEST_ASSERT_FALSE(mrp.hasPendingAck());
}

void test_ack_pending_when_peer_requests() {
    mrp.onReceived(100, true); // peer set R-flag (needs ack)
    TEST_ASSERT_TRUE(mrp.hasPendingAck());
    TEST_ASSERT_EQUAL(100, mrp.pendingAckCounter());
}

void test_ack_not_pending_when_not_requested() {
    mrp.onReceived(100, false);
    TEST_ASSERT_FALSE(mrp.hasPendingAck());
}

void test_ack_cleared_after_sent() {
    mrp.onReceived(100, true);
    TEST_ASSERT_TRUE(mrp.hasPendingAck());
    mrp.clearPendingAck();
    TEST_ASSERT_FALSE(mrp.hasPendingAck());
}

void test_ack_tracks_latest() {
    mrp.onReceived(100, true);
    mrp.onReceived(101, true);
    // Should track latest ack-requiring message
    TEST_ASSERT_EQUAL(101, mrp.pendingAckCounter());
}

// ============================================================================
// Retransmission ACK handling (Spec 4.11.6.2)
// ============================================================================

void test_retransmit_ack_clears_slot() {
    uint8_t data[] = {0x01, 0x02};
    mrp.trackSent(data, 2, 42);
    TEST_ASSERT_TRUE(mrp.hasActiveRetransmit());

    mrp.onAckReceived(42);
    TEST_ASSERT_FALSE(mrp.hasActiveRetransmit());
}

void test_retransmit_ack_wrong_counter_no_effect() {
    uint8_t data[] = {0x01};
    mrp.trackSent(data, 1, 42);
    mrp.onAckReceived(99); // Wrong counter
    TEST_ASSERT_TRUE(mrp.hasActiveRetransmit()); // Still active
}

void test_retransmit_multiple_slots() {
    uint8_t data1[] = {0x01};
    uint8_t data2[] = {0x02};
    mrp.trackSent(data1, 1, 10);
    mrp.trackSent(data2, 1, 11);
    TEST_ASSERT_TRUE(mrp.hasActiveRetransmit());

    mrp.onAckReceived(10);
    TEST_ASSERT_TRUE(mrp.hasActiveRetransmit()); // 11 still active

    mrp.onAckReceived(11);
    TEST_ASSERT_FALSE(mrp.hasActiveRetransmit());
}

// ============================================================================
// Reset
// ============================================================================

void test_reset_clears_all_state() {
    mrp.initCounter(500);
    mrp.onReceived(100, true);
    uint8_t data[] = {0x01};
    mrp.trackSent(data, 1, 42);

    mrp.reset();

    TEST_ASSERT_FALSE(mrp.hasPendingAck());
    TEST_ASSERT_FALSE(mrp.hasActiveRetransmit());
    TEST_ASSERT_FALSE(mrp.isDuplicate(100)); // Window cleared
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // Send counter
    RUN_TEST(test_send_counter_increments);
    RUN_TEST(test_send_counter_init_range);
    RUN_TEST(test_send_counter_sequential);

    // Deduplication
    RUN_TEST(test_dedup_first_message_not_duplicate);
    RUN_TEST(test_dedup_after_receive_same_is_duplicate);
    RUN_TEST(test_dedup_new_counter_not_duplicate);
    RUN_TEST(test_dedup_old_in_window);
    RUN_TEST(test_dedup_old_outside_window);
    RUN_TEST(test_dedup_sequential_messages);
    RUN_TEST(test_dedup_out_of_order_within_window);
    RUN_TEST(test_dedup_window_boundary_31);
    RUN_TEST(test_dedup_window_boundary_32);

    // ACK tracking
    RUN_TEST(test_ack_not_pending_initially);
    RUN_TEST(test_ack_pending_when_peer_requests);
    RUN_TEST(test_ack_not_pending_when_not_requested);
    RUN_TEST(test_ack_cleared_after_sent);
    RUN_TEST(test_ack_tracks_latest);

    // Retransmission
    RUN_TEST(test_retransmit_ack_clears_slot);
    RUN_TEST(test_retransmit_ack_wrong_counter_no_effect);
    RUN_TEST(test_retransmit_multiple_slots);

    // Reset
    RUN_TEST(test_reset_clears_all_state);

    return UNITY_END();
}
