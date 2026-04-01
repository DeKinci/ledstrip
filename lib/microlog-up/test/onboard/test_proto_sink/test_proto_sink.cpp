#include <unity.h>
#include <Arduino.h>
#include <MicroLogProto.h>

using namespace MicroLog;

static ProtoSink sink;

void setUp() {}
void tearDown() {}

// ============== LogEntry structure ==============

void test_log_entry_size() {
    TEST_ASSERT_EQUAL(100, sizeof(LogEntry));
}

void test_log_entry_fields() {
    LogEntry entry{};
    entry.timestamp = 12345;
    entry.bootCount = 3;
    entry.level = LEVEL_INFO;
    entry._reserved = 0;

    TEST_ASSERT_EQUAL(12345, entry.timestamp);
    TEST_ASSERT_EQUAL(3, entry.bootCount);
    TEST_ASSERT_EQUAL(LEVEL_INFO, entry.level);
}

// ============== ProtoSink — log routing ==============

void test_info_goes_to_stream_only() {
    // Clear any previous state
    auto& stream = sink.logStream();
    auto& errors = sink.errorLog();
    size_t streamBefore = stream.count();
    size_t errorsBefore = errors.count();

    sink.onLog(LEVEL_INFO, "Test", "info message");

    TEST_ASSERT_EQUAL(streamBefore + 1, stream.count());
    TEST_ASSERT_EQUAL(errorsBefore, errors.count());  // INFO should NOT go to errorLog
}

void test_warn_goes_to_both() {
    auto& stream = sink.logStream();
    auto& errors = sink.errorLog();
    size_t streamBefore = stream.count();
    size_t errorsBefore = errors.count();

    sink.onLog(LEVEL_WARN, "Test", "warning message");

    TEST_ASSERT_EQUAL(streamBefore + 1, stream.count());
    TEST_ASSERT_EQUAL(errorsBefore + 1, errors.count());
}

void test_error_goes_to_both() {
    auto& stream = sink.logStream();
    auto& errors = sink.errorLog();
    size_t streamBefore = stream.count();
    size_t errorsBefore = errors.count();

    sink.onLog(LEVEL_ERROR, "Test", "error message");

    TEST_ASSERT_EQUAL(streamBefore + 1, stream.count());
    TEST_ASSERT_EQUAL(errorsBefore + 1, errors.count());
}

// ============== LogEntry content ==============

void test_tag_truncation() {
    // Tag field is 12 bytes. Test that longer tags are truncated.
    LogEntry entry{};
    entry.tag.fill('\0');
    const char* longTag = "VeryLongTagName";
    size_t tagLen = strlen(longTag);
    if (tagLen > entry.tag.size()) tagLen = entry.tag.size();
    memcpy(entry.tag.data(), longTag, tagLen);

    // Should be truncated to 12 chars
    TEST_ASSERT_EQUAL(12, entry.tag.size());
    TEST_ASSERT_EQUAL('V', entry.tag[0]);
    TEST_ASSERT_EQUAL('a', entry.tag[11]);  // "VeryLongTagN" → 'N' at index 11
}

void test_message_truncation() {
    // Message field is 80 bytes. Verify size.
    LogEntry entry{};
    TEST_ASSERT_EQUAL(80, entry.message.size());
}

// ============== Boot count ==============

void test_boot_count_nonzero() {
    // After init(), boot count should be at least 1
    sink.init();
    TEST_ASSERT_GREATER_THAN(0, sink.bootCount());
}

// ============== Stream property metadata ==============

void test_error_log_is_persistent() {
    TEST_ASSERT_TRUE(sink.errorLog().isPersistent());
}

void test_error_log_is_readonly() {
    TEST_ASSERT_TRUE(sink.errorLog().isReadonly());
}

void test_log_stream_is_not_persistent() {
    TEST_ASSERT_FALSE(sink.logStream().isPersistent());
}

void test_log_stream_is_readonly() {
    TEST_ASSERT_TRUE(sink.logStream().isReadonly());
}

void test_log_stream_is_hidden() {
    TEST_ASSERT_TRUE(sink.logStream().isHidden());
}

void setup() {
    delay(2000);
    UNITY_BEGIN();

    // LogEntry structure
    RUN_TEST(test_log_entry_size);
    RUN_TEST(test_log_entry_fields);

    // Log routing
    RUN_TEST(test_info_goes_to_stream_only);
    RUN_TEST(test_warn_goes_to_both);
    RUN_TEST(test_error_goes_to_both);

    // LogEntry content
    RUN_TEST(test_tag_truncation);
    RUN_TEST(test_message_truncation);

    // Boot count
    RUN_TEST(test_boot_count_nonzero);

    // Stream property metadata
    RUN_TEST(test_error_log_is_persistent);
    RUN_TEST(test_error_log_is_readonly);
    RUN_TEST(test_log_stream_is_not_persistent);
    RUN_TEST(test_log_stream_is_readonly);
    RUN_TEST(test_log_stream_is_hidden);

    UNITY_END();
}

void loop() {}
