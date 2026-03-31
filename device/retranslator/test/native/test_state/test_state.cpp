#include <unity.h>
#include "state.h"
#include "state.cpp"

// --- Helper to create a MessageEntry ---
static MessageEntry makeEntry(uint8_t senderId, uint16_t seq, MsgType type = MsgType::Text,
                               uint32_t timestamp = 1000) {
    MessageEntry e;
    e.senderId = senderId;
    e.seq = seq;
    e.timestamp = timestamp;
    e.msgType = type;
    e.payloadLen = 0;
    e.valid = true;
    return e;
}

static MessageEntry makeLocation(uint8_t senderId, uint16_t seq, uint8_t nodeA, uint8_t nodeB) {
    MessageEntry e = makeEntry(senderId, seq, MsgType::Location);
    e.payload[0] = nodeA;
    e.payload[1] = nodeB;
    e.payloadLen = 2;
    return e;
}

// --- SenderLog tests ---

void test_sender_log_add_and_get() {
    SenderLog log;
    log.senderId = 1;
    log.active = true;

    auto e1 = makeEntry(1, 1);
    auto e2 = makeEntry(1, 2);
    auto e3 = makeEntry(1, 3);

    TEST_ASSERT_TRUE(log.addMessage(e1));
    TEST_ASSERT_TRUE(log.addMessage(e2));
    TEST_ASSERT_TRUE(log.addMessage(e3));

    TEST_ASSERT_EQUAL(3, log.msgCount);
    TEST_ASSERT_EQUAL(3, log.highSeq);

    const MessageEntry* found = log.getMessage(2);
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL(2, found->seq);

    TEST_ASSERT_NULL(log.getMessage(99));
}

void test_sender_log_dedup() {
    SenderLog log;
    log.senderId = 1;
    log.active = true;

    auto e1 = makeEntry(1, 5);
    TEST_ASSERT_TRUE(log.addMessage(e1));
    TEST_ASSERT_FALSE(log.addMessage(e1)); // duplicate
    TEST_ASSERT_EQUAL(1, log.msgCount);
}

void test_sender_log_location_tracking() {
    SenderLog log;
    log.senderId = 1;
    log.active = true;

    auto loc = makeLocation(1, 1, 3, 7);
    log.addMessage(loc);

    TEST_ASSERT_EQUAL(1, log.locSeq);
    TEST_ASSERT_EQUAL(3, log.nodeA);
    TEST_ASSERT_EQUAL(7, log.nodeB);
}

void test_sender_log_ring_buffer_wrap() {
    SenderLog log;
    log.senderId = 1;
    log.active = true;

    // Fill beyond capacity
    for (uint16_t i = 1; i <= MSGS_PER_SENDER + 10; i++) {
        log.addMessage(makeEntry(1, i));
    }

    TEST_ASSERT_EQUAL(MSGS_PER_SENDER, log.msgCount);
    TEST_ASSERT_EQUAL(MSGS_PER_SENDER + 10, log.highSeq);

    // Oldest messages should be gone
    TEST_ASSERT_NULL(log.getMessage(1));
    TEST_ASSERT_NULL(log.getMessage(10));

    // Recent messages should be present
    TEST_ASSERT_NOT_NULL(log.getMessage(MSGS_PER_SENDER + 10));
    TEST_ASSERT_NOT_NULL(log.getMessage(11));
}

void test_sender_log_low_seq() {
    SenderLog log;
    log.senderId = 1;
    log.active = true;

    log.addMessage(makeEntry(1, 5));
    log.addMessage(makeEntry(1, 6));
    log.addMessage(makeEntry(1, 7));

    TEST_ASSERT_EQUAL(5, log.lowSeq());
}

// --- MessageStore tests ---

void test_store_message() {
    MessageStore store;
    auto e1 = makeEntry(1, 1);
    auto e2 = makeEntry(2, 1);

    TEST_ASSERT_TRUE(store.storeMessage(e1, 1000));
    TEST_ASSERT_TRUE(store.storeMessage(e2, 1000));

    TEST_ASSERT_EQUAL(2, store.activeSenderCount());

    SenderLog* s1 = store.getSender(1);
    TEST_ASSERT_NOT_NULL(s1);
    TEST_ASSERT_EQUAL(1, s1->highSeq);
    TEST_ASSERT_EQUAL(1000, s1->lastHeardMs);
}

void test_store_dedup() {
    MessageStore store;
    auto e = makeEntry(1, 5);

    TEST_ASSERT_TRUE(store.storeMessage(e, 1000));
    TEST_ASSERT_FALSE(store.storeMessage(e, 2000)); // dup
    TEST_ASSERT_EQUAL(1, store.getSender(1)->msgCount);
}

void test_store_full_senders() {
    MessageStore store;
    // Fill all sender slots
    for (uint8_t i = 0; i < MAX_SENDERS; i++) {
        store.storeMessage(makeEntry(i + 1, 1), 1000);
    }
    TEST_ASSERT_EQUAL(MAX_SENDERS, store.activeSenderCount());

    // One more should fail
    auto overflow = makeEntry(MAX_SENDERS + 1, 1);
    TEST_ASSERT_FALSE(store.storeMessage(overflow, 1000));
}

// --- State hash tests ---

void test_state_hash_changes() {
    MessageStore store;
    uint16_t h0 = store.stateHash(500);

    store.storeMessage(makeEntry(1, 1), 1000);
    uint16_t h1 = store.stateHash(2000);
    TEST_ASSERT_NOT_EQUAL(h0, h1);

    store.storeMessage(makeEntry(1, 2), 2000);
    uint16_t h2 = store.stateHash(3000);
    TEST_ASSERT_NOT_EQUAL(h1, h2);
}

void test_state_hash_same_data() {
    MessageStore a, b;
    a.storeMessage(makeEntry(1, 1), 1000);
    a.storeMessage(makeEntry(2, 3), 1000);

    b.storeMessage(makeEntry(1, 1), 2000);
    b.storeMessage(makeEntry(2, 3), 2000);

    TEST_ASSERT_EQUAL(a.stateHash(3000), b.stateHash(3000));
}

// --- Digest encode/decode ---

void test_digest_round_trip() {
    MessageStore store;
    store.storeMessage(makeEntry(1, 5), 1000);
    store.storeMessage(makeEntry(2, 10), 1000);
    store.storeMessage(makeLocation(1, 3, 1, 2), 1500);

    uint8_t buf[64];
    size_t len = store.encodeDigest(buf, sizeof(buf), 2000);
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_EQUAL(1 + 2 * 5, len); // 2 senders

    // A peer with partial data decodes digest and computes needs
    MessageStore peer;
    peer.storeMessage(makeEntry(1, 3), 500); // peer only has up to seq 3 for sender 1

    SyncNeeds needs;
    peer.decodeDigest(buf, len, needs);

    // Peer should need sender 1 seq 4-5, and sender 2 seq 1-10
    TEST_ASSERT_EQUAL(2, needs.count);

    // Find sender 1's need
    bool foundSender1 = false, foundSender2 = false;
    for (uint8_t i = 0; i < needs.count; i++) {
        if (needs.entries[i].senderId == 1) {
            TEST_ASSERT_EQUAL(4, needs.entries[i].fromSeq); // localHigh(3) + 1
            TEST_ASSERT_EQUAL(5, needs.entries[i].toSeq);
            foundSender1 = true;
        }
        if (needs.entries[i].senderId == 2) {
            TEST_ASSERT_EQUAL(1, needs.entries[i].fromSeq);
            TEST_ASSERT_EQUAL(10, needs.entries[i].toSeq);
            foundSender2 = true;
        }
    }
    TEST_ASSERT_TRUE(foundSender1);
    TEST_ASSERT_TRUE(foundSender2);
}

void test_digest_no_needs() {
    MessageStore store;
    store.storeMessage(makeEntry(1, 5), 1000);

    uint8_t buf[64];
    size_t len = store.encodeDigest(buf, sizeof(buf), 2000);

    // Peer already has everything
    MessageStore peer;
    peer.storeMessage(makeEntry(1, 5), 500);
    peer.storeMessage(makeEntry(1, 6), 600); // peer even has more

    SyncNeeds needs;
    peer.decodeDigest(buf, len, needs);
    TEST_ASSERT_EQUAL(0, needs.count);
}

// --- SyncRequest encode/decode ---

void test_sync_request_round_trip() {
    SyncNeeds original;
    original.add(1, 4, 8);
    original.add(3, 1, 15);

    uint8_t buf[64];
    size_t len = MessageStore::encodeSyncRequest(original, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(1 + 2 * 5, len);

    SyncNeeds decoded;
    MessageStore::decodeSyncRequest(buf, len, decoded);
    TEST_ASSERT_EQUAL(2, decoded.count);
    TEST_ASSERT_EQUAL(1, decoded.entries[0].senderId);
    TEST_ASSERT_EQUAL(4, decoded.entries[0].fromSeq);
    TEST_ASSERT_EQUAL(8, decoded.entries[0].toSeq);
    TEST_ASSERT_EQUAL(3, decoded.entries[1].senderId);
    TEST_ASSERT_EQUAL(1, decoded.entries[1].fromSeq);
    TEST_ASSERT_EQUAL(15, decoded.entries[1].toSeq);
}

// --- forEachSender ---

void test_for_each_sender() {
    MessageStore store;
    store.storeMessage(makeEntry(5, 1), 100);
    store.storeMessage(makeEntry(10, 1), 200);

    uint8_t count = 0;
    store.forEachSender([&](const SenderLog& s) {
        count++;
        TEST_ASSERT_TRUE(s.senderId == 5 || s.senderId == 10);
    });
    TEST_ASSERT_EQUAL(2, count);
}

// --- New tests for review fixes ---

void test_sender_log_rejects_evicted_seq() {
    SenderLog log;
    log.senderId = 1;
    log.active = true;

    // Fill beyond capacity so early seqs get evicted
    for (uint16_t i = 1; i <= MSGS_PER_SENDER + 10; i++) {
        log.addMessage(makeEntry(1, i));
    }
    // seq 1 and 10 are evicted from buffer but highSeq=74, so they're rejected
    TEST_ASSERT_FALSE(log.addMessage(makeEntry(1, 1)));
    TEST_ASSERT_FALSE(log.addMessage(makeEntry(1, 10)));
}

void test_state_hash_commutative() {
    MessageStore a, b;
    a.storeMessage(makeEntry(1, 5), 1000);
    a.storeMessage(makeEntry(2, 3), 1000);
    b.storeMessage(makeEntry(2, 3), 1000);  // reverse order
    b.storeMessage(makeEntry(1, 5), 1000);
    TEST_ASSERT_EQUAL(a.stateHash(2000), b.stateHash(2000));
}

void test_state_hash_excludes_offline() {
    MessageStore store;
    store.storeMessage(makeEntry(1, 1), 1000);
    uint16_t h1 = store.stateHash(2000);  // sender online
    uint16_t h2 = store.stateHash(1000 + PRESENCE_TIMEOUT_MS + 1);  // sender offline
    TEST_ASSERT_NOT_EQUAL(h1, h2);
    TEST_ASSERT_EQUAL(0, h2);  // no active online senders → hash is 0
}

void test_purge_expired() {
    MessageStore store;
    store.storeMessage(makeEntry(1, 1), 1000);
    TEST_ASSERT_EQUAL(1, store.activeSenderCount());

    store.purgeExpired(1000 + PRESENCE_EXPIRE_MS + 1);
    TEST_ASSERT_EQUAL(0, store.activeSenderCount());
}

void test_update_presence() {
    MessageStore store;
    store.storeMessage(makeEntry(1, 1), 1000);
    store.updatePresence(1, 5000);
    const SenderLog* s = store.getSender(1);
    TEST_ASSERT_EQUAL(5000, s->lastHeardMs);

    // updatePresence on unknown sender does nothing (no crash)
    store.updatePresence(99, 5000);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_sender_log_add_and_get);
    RUN_TEST(test_sender_log_dedup);
    RUN_TEST(test_sender_log_location_tracking);
    RUN_TEST(test_sender_log_ring_buffer_wrap);
    RUN_TEST(test_sender_log_low_seq);
    RUN_TEST(test_store_message);
    RUN_TEST(test_store_dedup);
    RUN_TEST(test_store_full_senders);
    RUN_TEST(test_state_hash_changes);
    RUN_TEST(test_state_hash_same_data);
    RUN_TEST(test_digest_round_trip);
    RUN_TEST(test_digest_no_needs);
    RUN_TEST(test_sync_request_round_trip);
    RUN_TEST(test_for_each_sender);
    RUN_TEST(test_sender_log_rejects_evicted_seq);
    RUN_TEST(test_state_hash_commutative);
    RUN_TEST(test_state_hash_excludes_offline);
    RUN_TEST(test_purge_expired);
    RUN_TEST(test_update_presence);
    return UNITY_END();
}
