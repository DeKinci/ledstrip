#ifdef NATIVE_TEST

#include <unity.h>
#include <GestureDetector.h>
#include <vector>

// --- Test helpers ---

static std::vector<BasicGesture> gestures;
static std::vector<SequenceDetector::Action> actions;

static void collectGesture(BasicGesture g) { gestures.push_back(g); }
static void collectAction(SequenceDetector::Action a) { actions.push_back(a); }

void setUp(void) {
    mockMillis(0);
    gestures.clear();
    actions.clear();
}

void tearDown(void) {}

// ============================================================
// GestureDetector tests
// ============================================================

void test_click() {
    GestureDetector gd;
    gd.setCallback(collectGesture);

    mockMillis(100);
    gd.onPress();

    mockMillis(150);  // 50ms < clickMaxMs (200ms)
    gd.onRelease();

    TEST_ASSERT_EQUAL(1, gestures.size());
    TEST_ASSERT_EQUAL(BasicGesture::CLICK, gestures[0]);
}

void test_long_click() {
    GestureDetector gd;
    gd.setCallback(collectGesture);

    mockMillis(100);
    gd.onPress();

    // 250ms > clickMaxMs (200ms) but < holdThresholdMs (400ms)
    mockMillis(350);
    gd.loop();  // not yet hold
    gd.onRelease();

    TEST_ASSERT_EQUAL(1, gestures.size());
    TEST_ASSERT_EQUAL(BasicGesture::LONG_CLICK, gestures[0]);
}

void test_hold_start_and_tick() {
    GestureDetector gd;
    gd.setCallback(collectGesture);

    mockMillis(0);
    gd.onPress();

    // At 400ms, hold starts
    mockMillis(400);
    gd.loop();
    TEST_ASSERT_EQUAL(1, gestures.size());
    TEST_ASSERT_EQUAL(BasicGesture::HOLD_START, gestures[0]);

    // At 450ms (50ms tick interval), first tick
    mockMillis(450);
    gd.loop();
    TEST_ASSERT_EQUAL(2, gestures.size());
    TEST_ASSERT_EQUAL(BasicGesture::HOLD_TICK, gestures[1]);

    // At 500ms, second tick
    mockMillis(500);
    gd.loop();
    TEST_ASSERT_EQUAL(3, gestures.size());
    TEST_ASSERT_EQUAL(BasicGesture::HOLD_TICK, gestures[2]);
}

void test_hold_end() {
    GestureDetector gd;
    gd.setCallback(collectGesture);

    mockMillis(0);
    gd.onPress();
    mockMillis(500);
    gd.loop();  // HOLD_START + HOLD_TICK

    gestures.clear();
    gd.onRelease();

    TEST_ASSERT_EQUAL(1, gestures.size());
    TEST_ASSERT_EQUAL(BasicGesture::HOLD_END, gestures[0]);
}

void test_debounce_double_press() {
    GestureDetector gd;
    gd.setCallback(collectGesture);

    mockMillis(0);
    gd.onPress();
    gd.onPress();  // ignored — already pressed
    TEST_ASSERT_TRUE(gd.isPressed());

    mockMillis(50);
    gd.onRelease();
    gd.onRelease();  // ignored — already released
    TEST_ASSERT_FALSE(gd.isPressed());

    TEST_ASSERT_EQUAL(1, gestures.size());
    TEST_ASSERT_EQUAL(BasicGesture::CLICK, gestures[0]);
}

void test_no_tick_before_hold_threshold() {
    GestureDetector gd;
    gd.setCallback(collectGesture);

    mockMillis(0);
    gd.onPress();

    mockMillis(100);
    gd.loop();
    mockMillis(200);
    gd.loop();
    mockMillis(300);
    gd.loop();

    // No gestures yet — still under holdThresholdMs
    TEST_ASSERT_EQUAL(0, gestures.size());
}

void test_is_holding() {
    GestureDetector gd;
    gd.setCallback(collectGesture);

    mockMillis(0);
    gd.onPress();
    TEST_ASSERT_TRUE(gd.isPressed());
    TEST_ASSERT_FALSE(gd.isHolding());

    mockMillis(400);
    gd.loop();
    TEST_ASSERT_TRUE(gd.isHolding());

    gd.onRelease();
    TEST_ASSERT_FALSE(gd.isPressed());
    TEST_ASSERT_FALSE(gd.isHolding());
}

void test_custom_config() {
    GestureDetector gd;
    gd.setConfig({.clickMaxMs = 100, .holdThresholdMs = 200, .holdTickMs = 25});
    gd.setCallback(collectGesture);

    // 80ms press = click with 100ms threshold
    mockMillis(0);
    gd.onPress();
    mockMillis(80);
    gd.onRelease();
    TEST_ASSERT_EQUAL(BasicGesture::CLICK, gestures[0]);

    gestures.clear();

    // 150ms press = long click (> 100ms click, < 200ms hold)
    mockMillis(1000);
    gd.onPress();
    mockMillis(1150);
    gd.loop();
    gd.onRelease();
    TEST_ASSERT_EQUAL(BasicGesture::LONG_CLICK, gestures[0]);
}

// ============================================================
// SequenceDetector tests
// ============================================================

void test_single_click_after_timeout() {
    GestureDetector gd;
    SequenceDetector sd;
    gd.setCallback([&](BasicGesture g) { sd.onGesture(g); });
    sd.setCallback(collectAction);

    // Press and quick release = click
    mockMillis(0);
    gd.onPress();
    sd.onPress();
    mockMillis(50);
    gd.onRelease();
    sd.onRelease();

    // Not yet — waiting for potential double click
    TEST_ASSERT_EQUAL(0, actions.size());

    // After doubleClickWindowMs (300ms)
    mockMillis(400);
    sd.loop();

    TEST_ASSERT_EQUAL(1, actions.size());
    TEST_ASSERT_EQUAL(SequenceDetector::Action::SINGLE_CLICK, actions[0]);
}

void test_double_click() {
    GestureDetector gd;
    SequenceDetector sd;
    gd.setCallback([&](BasicGesture g) { sd.onGesture(g); });
    sd.setCallback(collectAction);

    // First click
    mockMillis(0);
    gd.onPress(); sd.onPress();
    mockMillis(50);
    gd.onRelease(); sd.onRelease();

    // Second click within 300ms window
    mockMillis(150);
    gd.onPress(); sd.onPress();
    mockMillis(200);
    gd.onRelease(); sd.onRelease();

    TEST_ASSERT_EQUAL(1, actions.size());
    TEST_ASSERT_EQUAL(SequenceDetector::Action::DOUBLE_CLICK, actions[0]);
}

void test_hold_tick_action() {
    GestureDetector gd;
    SequenceDetector sd;
    gd.setCallback([&](BasicGesture g) { sd.onGesture(g); });
    sd.setCallback(collectAction);

    mockMillis(0);
    gd.onPress(); sd.onPress();

    // Hold past threshold
    mockMillis(400);
    gd.loop();  // HOLD_START

    // First tick
    mockMillis(450);
    gd.loop();  // HOLD_TICK

    // Should have HOLD_TICK action
    bool hasHoldTick = false;
    for (auto& a : actions) {
        if (a == SequenceDetector::Action::HOLD_TICK) hasHoldTick = true;
    }
    TEST_ASSERT_TRUE(hasHoldTick);
}

void test_click_hold_tick_action() {
    GestureDetector gd;
    SequenceDetector sd;
    gd.setCallback([&](BasicGesture g) { sd.onGesture(g); });
    sd.setCallback(collectAction);

    // First click
    mockMillis(0);
    gd.onPress(); sd.onPress();
    mockMillis(50);
    gd.onRelease(); sd.onRelease();

    // Second press — hold (within double click window)
    mockMillis(200);
    gd.onPress(); sd.onPress();

    // Hold past threshold
    mockMillis(600);
    gd.loop();  // HOLD_START

    mockMillis(650);
    gd.loop();  // HOLD_TICK

    bool hasClickHoldTick = false;
    for (auto& a : actions) {
        if (a == SequenceDetector::Action::CLICK_HOLD_TICK) hasClickHoldTick = true;
    }
    TEST_ASSERT_TRUE(hasClickHoldTick);
}

void test_hold_end_action() {
    GestureDetector gd;
    SequenceDetector sd;
    gd.setCallback([&](BasicGesture g) { sd.onGesture(g); });
    sd.setCallback(collectAction);

    mockMillis(0);
    gd.onPress(); sd.onPress();
    mockMillis(500);
    gd.loop();  // HOLD_START + HOLD_TICK

    actions.clear();
    gd.onRelease(); sd.onRelease();

    TEST_ASSERT_EQUAL(1, actions.size());
    TEST_ASSERT_EQUAL(SequenceDetector::Action::HOLD_END, actions[0]);
}

void test_long_click_becomes_single_click() {
    GestureDetector gd;
    SequenceDetector sd;
    gd.setCallback([&](BasicGesture g) { sd.onGesture(g); });
    sd.setCallback(collectAction);

    // Press for 250ms — long click (> clickMax, < holdThreshold)
    mockMillis(0);
    gd.onPress(); sd.onPress();
    mockMillis(250);
    gd.loop();
    gd.onRelease(); sd.onRelease();

    // Long click emits SINGLE_CLICK immediately (no timeout wait)
    TEST_ASSERT_EQUAL(1, actions.size());
    TEST_ASSERT_EQUAL(SequenceDetector::Action::SINGLE_CLICK, actions[0]);
}

void test_no_single_click_during_press() {
    // Single click timeout should not fire while button is still pressed
    GestureDetector gd;
    SequenceDetector sd;
    gd.setCallback([&](BasicGesture g) { sd.onGesture(g); });
    sd.setCallback(collectAction);

    // Click
    mockMillis(0);
    gd.onPress(); sd.onPress();
    mockMillis(50);
    gd.onRelease(); sd.onRelease();

    // Immediately press again and hold past the double-click window
    mockMillis(100);
    gd.onPress(); sd.onPress();

    // At 500ms — well past doubleClickWindowMs, but still pressed
    mockMillis(500);
    sd.loop();
    gd.loop();  // This will trigger HOLD_START

    // Should NOT have emitted a spurious SINGLE_CLICK
    bool hasSingleClick = false;
    for (auto& a : actions) {
        if (a == SequenceDetector::Action::SINGLE_CLICK) hasSingleClick = true;
    }
    TEST_ASSERT_FALSE(hasSingleClick);
}

// ============================================================

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // GestureDetector
    RUN_TEST(test_click);
    RUN_TEST(test_long_click);
    RUN_TEST(test_hold_start_and_tick);
    RUN_TEST(test_hold_end);
    RUN_TEST(test_debounce_double_press);
    RUN_TEST(test_no_tick_before_hold_threshold);
    RUN_TEST(test_is_holding);
    RUN_TEST(test_custom_config);

    // SequenceDetector
    RUN_TEST(test_single_click_after_timeout);
    RUN_TEST(test_double_click);
    RUN_TEST(test_hold_tick_action);
    RUN_TEST(test_click_hold_tick_action);
    RUN_TEST(test_hold_end_action);
    RUN_TEST(test_long_click_becomes_single_click);
    RUN_TEST(test_no_single_click_during_press);

    return UNITY_END();
}

#endif // NATIVE_TEST
