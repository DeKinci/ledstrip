#include <unity.h>
#include <Gauge.h>

using namespace MicroLog;

void setUp() {}
void tearDown() {}

// ============== Gauge — basic set/get ==============

void test_gauge_default_value() {
    Gauge<uint32_t> g("heap", "B");
    TEST_ASSERT_EQUAL(0, g.get());
}

void test_gauge_set_get() {
    Gauge<uint32_t> g("heap", "B");
    g.set(42000);
    TEST_ASSERT_EQUAL(42000, g.get());
}

void test_gauge_overwrite() {
    Gauge<int32_t> g("temp", "C");
    g.set(25);
    g.set(-10);
    TEST_ASSERT_EQUAL(-10, g.get());
}

// ============== Gauge — printValue ==============

void test_gauge_print_no_value() {
    Gauge<uint32_t> g("heap", "B");
    char buf[32];
    int n = g.printValue(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("heap=-", buf);
    TEST_ASSERT_EQUAL(6, n);
}

void test_gauge_print_unsigned_with_unit() {
    Gauge<uint32_t> g("heap", "B");
    g.set(125000);
    char buf[32];
    g.printValue(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("heap=125000B", buf);
}

void test_gauge_print_unsigned_no_unit() {
    Gauge<uint32_t> g("clients", nullptr);
    g.set(3);
    char buf[32];
    g.printValue(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("clients=3", buf);
}

void test_gauge_print_signed_with_unit() {
    Gauge<int32_t> g("rssi", "dBm");
    g.set(-45);
    char buf[32];
    g.printValue(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("rssi=-45dBm", buf);
}

void test_gauge_print_signed_no_unit() {
    Gauge<int32_t> g("offset", nullptr);
    g.set(-7);
    char buf[32];
    g.printValue(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("offset=-7", buf);
}

void test_gauge_print_float_with_unit() {
    Gauge<float> g("voltage", "V");
    g.set(3.3f);
    char buf[32];
    g.printValue(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("voltage=3.3V", buf);
}

void test_gauge_print_float_no_unit() {
    Gauge<float> g("ratio", nullptr);
    g.set(0.5f);
    char buf[32];
    g.printValue(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("ratio=0.5", buf);
}

void test_gauge_print_truncated() {
    Gauge<uint32_t> g("heap", "B");
    g.set(125000);
    char buf[6]; // too small for "heap=125000B"
    g.printValue(buf, sizeof(buf));
    // snprintf truncates but null-terminates
    TEST_ASSERT_EQUAL_STRING("heap=", buf);
}

// ============== Gauge — callback ==============

void test_gauge_callback_fires_on_set() {
    static uint32_t cbTimestamp = 0;
    static uint32_t cbValue = 0;
    static int cbCount = 0;

    Gauge<uint32_t> g("test", nullptr);
    g.onPush([](uint32_t ts, uint32_t val) {
        cbTimestamp = ts;
        cbValue = val;
        cbCount++;
    });

    cbCount = 0;
    g.set(42);
    TEST_ASSERT_EQUAL(1, cbCount);
    TEST_ASSERT_EQUAL(42, cbValue);
}

void test_gauge_callback_not_on_same_value() {
    // On native, millis() returns 0 and debounce is 1000ms by default.
    // Setting same value with same timestamp should not fire again.
    static int cbCount = 0;

    Gauge<uint32_t> g("test", nullptr, 1000);
    g.onPush([](uint32_t, uint32_t) { cbCount++; });

    cbCount = 0;
    g.set(42);
    TEST_ASSERT_EQUAL(1, cbCount);
    g.set(42); // same value, same time — debounced
    TEST_ASSERT_EQUAL(1, cbCount);
}

void test_gauge_callback_on_value_change() {
    static int cbCount = 0;

    Gauge<uint32_t> g("test", nullptr, 1000);
    g.onPush([](uint32_t, uint32_t) { cbCount++; });

    cbCount = 0;
    g.set(1);
    g.set(2); // different value — fires
    TEST_ASSERT_EQUAL(2, cbCount);
}

void test_gauge_no_callback_if_not_set() {
    static int cbCount = 0;

    Gauge<uint32_t> g("test", nullptr);
    g.set(42);
    // No callback registered — should not crash
    TEST_ASSERT_EQUAL(42, g.get());
}

// ============== Gauge — metadata ==============

void test_gauge_name() {
    Gauge<uint32_t> g("heap", "B");
    TEST_ASSERT_EQUAL_STRING("heap", g.name());
}

void test_gauge_unit() {
    Gauge<uint32_t> g("heap", "B");
    TEST_ASSERT_EQUAL_STRING("B", g.unit());
}

void test_gauge_null_unit() {
    Gauge<uint32_t> g("count", nullptr);
    TEST_ASSERT_NULL(g.unit());
}

// ============== Counter — basic ==============

void test_counter_default_zero() {
    Counter<uint32_t> c("reqs");
    TEST_ASSERT_EQUAL(0, c.get());
}

void test_counter_increment_default() {
    Counter<uint32_t> c("reqs");
    c.increment();
    c.increment();
    c.increment();
    TEST_ASSERT_EQUAL(3, c.get());
}

void test_counter_increment_custom_delta() {
    Counter<uint32_t> c("bytes", "B");
    c.increment(100);
    c.increment(256);
    TEST_ASSERT_EQUAL(356, c.get());
}

void test_counter_signed() {
    Counter<int32_t> c("balance");
    c.increment(10);
    c.increment(-3);
    TEST_ASSERT_EQUAL(7, c.get());
}

// ============== Counter — printValue ==============

void test_counter_print_unsigned_with_unit() {
    Counter<uint32_t> c("bytes", "B");
    c.increment(1024);
    char buf[32];
    c.printValue(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("bytes=1024B", buf);
}

void test_counter_print_unsigned_no_unit() {
    Counter<uint32_t> c("reqs", nullptr);
    c.increment(5);
    char buf[32];
    c.printValue(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("reqs=5", buf);
}

void test_counter_print_signed_with_unit() {
    Counter<int32_t> c("delta", "ms");
    c.increment(-50);
    char buf[32];
    c.printValue(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("delta=-50ms", buf);
}

void test_counter_print_zero() {
    Counter<uint32_t> c("empty");
    char buf[32];
    c.printValue(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("empty=0", buf);
}

// ============== Counter — metadata ==============

void test_counter_name() {
    Counter<uint32_t> c("reqs");
    TEST_ASSERT_EQUAL_STRING("reqs", c.name());
}

void test_counter_unit() {
    Counter<uint32_t> c("bytes", "B");
    TEST_ASSERT_EQUAL_STRING("B", c.unit());
}

// ============== Counter — callback ==============

void test_counter_callback() {
    static uint32_t cbValue = 0;
    static int cbCount = 0;

    Counter<uint32_t> c("reqs");
    c.onPush([](uint32_t, uint32_t val) {
        cbValue = val;
        cbCount++;
    });

    // Counter doesn't call callback on increment (only Gauge calls on set)
    // Verify increment works regardless
    cbCount = 0;
    c.increment(5);
    TEST_ASSERT_EQUAL(5, c.get());
}

int main() {
    UNITY_BEGIN();

    // Gauge basic
    RUN_TEST(test_gauge_default_value);
    RUN_TEST(test_gauge_set_get);
    RUN_TEST(test_gauge_overwrite);

    // Gauge printValue
    RUN_TEST(test_gauge_print_no_value);
    RUN_TEST(test_gauge_print_unsigned_with_unit);
    RUN_TEST(test_gauge_print_unsigned_no_unit);
    RUN_TEST(test_gauge_print_signed_with_unit);
    RUN_TEST(test_gauge_print_signed_no_unit);
    RUN_TEST(test_gauge_print_float_with_unit);
    RUN_TEST(test_gauge_print_float_no_unit);
    RUN_TEST(test_gauge_print_truncated);

    // Gauge callback
    RUN_TEST(test_gauge_callback_fires_on_set);
    RUN_TEST(test_gauge_callback_not_on_same_value);
    RUN_TEST(test_gauge_callback_on_value_change);
    RUN_TEST(test_gauge_no_callback_if_not_set);

    // Gauge metadata
    RUN_TEST(test_gauge_name);
    RUN_TEST(test_gauge_unit);
    RUN_TEST(test_gauge_null_unit);

    // Counter basic
    RUN_TEST(test_counter_default_zero);
    RUN_TEST(test_counter_increment_default);
    RUN_TEST(test_counter_increment_custom_delta);
    RUN_TEST(test_counter_signed);

    // Counter printValue
    RUN_TEST(test_counter_print_unsigned_with_unit);
    RUN_TEST(test_counter_print_unsigned_no_unit);
    RUN_TEST(test_counter_print_signed_with_unit);
    RUN_TEST(test_counter_print_zero);

    // Counter metadata
    RUN_TEST(test_counter_name);
    RUN_TEST(test_counter_unit);
    RUN_TEST(test_counter_callback);

    return UNITY_END();
}
