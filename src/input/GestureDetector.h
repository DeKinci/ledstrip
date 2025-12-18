#ifndef GESTURE_DETECTOR_H
#define GESTURE_DETECTOR_H

#include <Arduino.h>

// Basic gesture types
enum class BasicGesture {
    CLICK,        // Short press-release
    LONG_CLICK,   // Longer press-release (but released before hold threshold)
    HOLD_START,   // Press held past threshold
    HOLD_TICK,    // Still holding (repeated for ramping)
    HOLD_END,     // Released after holding
};

// Modular gesture detector - emits basic gestures from press/release events
// Sequences (double-click, click+hold, etc.) are built on top of these
class GestureDetector {
public:
    struct Config {
        uint32_t clickMaxMs = 200;       // Max press duration for CLICK
        uint32_t holdThresholdMs = 400;  // Time before HOLD_START
        uint32_t holdTickMs = 50;        // Tick interval during hold (20Hz)
    };

    using Callback = void(*)(BasicGesture);

    GestureDetector() = default;

    void setCallback(Callback cb) { callback = cb; }
    void setConfig(const Config& cfg) { config = cfg; }

    void onPress() {
        if (pressed) return;  // Debounce
        pressed = true;
        pressTime = millis();
        holdStarted = false;
    }

    void onRelease() {
        if (!pressed) return;  // Debounce
        pressed = false;
        uint32_t duration = millis() - pressTime;

        if (holdStarted) {
            emit(BasicGesture::HOLD_END);
        } else if (duration < config.clickMaxMs) {
            emit(BasicGesture::CLICK);
        } else {
            emit(BasicGesture::LONG_CLICK);
        }
    }

    // Must be called regularly
    void loop() {
        if (!pressed) return;

        uint32_t now = millis();
        uint32_t duration = now - pressTime;

        if (!holdStarted && duration >= config.holdThresholdMs) {
            holdStarted = true;
            emit(BasicGesture::HOLD_START);
            lastTickTime = now;
        }

        if (holdStarted && (now - lastTickTime) >= config.holdTickMs) {
            emit(BasicGesture::HOLD_TICK);
            lastTickTime = now;
        }
    }

    bool isPressed() const { return pressed; }
    bool isHolding() const { return pressed && holdStarted; }

private:
    void emit(BasicGesture g) {
        if (callback) callback(g);
    }

    bool pressed = false;
    bool holdStarted = false;
    uint32_t pressTime = 0;
    uint32_t lastTickTime = 0;
    Callback callback = nullptr;
    Config config;
};

// Sequence detector - builds patterns from basic gestures
// Detects: single click, double click, hold, click+hold
class SequenceDetector {
public:
    enum class Action {
        NONE,
        SINGLE_CLICK,    // One click (after timeout)
        DOUBLE_CLICK,    // Two clicks in quick succession
        HOLD_TICK,       // Holding (first press, ramping)
        CLICK_HOLD_TICK, // Click then hold (ramping opposite direction)
        HOLD_END,        // Any hold released
    };

    struct Config {
        uint32_t doubleClickWindowMs = 300;  // Time to wait for second click
    };

    using Callback = void(*)(Action);

    void setCallback(Callback cb) { callback = cb; }
    void setConfig(const Config& cfg) { config = cfg; }

    // Track press state (call same as GestureDetector)
    void onPress() {
        pressed = true;
    }

    void onRelease() {
        pressed = false;
    }

    // Feed basic gestures into sequence detector
    void onGesture(BasicGesture g) {
        uint32_t now = millis();

        switch (g) {
            case BasicGesture::CLICK:
                if (pendingClick && (now - lastClickTime) < config.doubleClickWindowMs) {
                    // Second click = double click
                    pendingClick = false;
                    emit(Action::DOUBLE_CLICK);
                } else {
                    // First click - wait for potential second
                    pendingClick = true;
                    lastClickTime = now;
                }
                hadClickBeforeHold = false;
                break;

            case BasicGesture::LONG_CLICK:
                // Long click cancels pending click, treat as single
                if (pendingClick) {
                    pendingClick = false;
                }
                emit(Action::SINGLE_CLICK);
                hadClickBeforeHold = false;
                break;

            case BasicGesture::HOLD_START:
                // Check if we had a click before this hold
                hadClickBeforeHold = pendingClick;
                pendingClick = false;
                break;

            case BasicGesture::HOLD_TICK:
                if (hadClickBeforeHold) {
                    emit(Action::CLICK_HOLD_TICK);
                } else {
                    emit(Action::HOLD_TICK);
                }
                break;

            case BasicGesture::HOLD_END:
                emit(Action::HOLD_END);
                hadClickBeforeHold = false;
                break;
        }
    }

    // Must be called regularly to timeout pending clicks
    void loop() {
        // Don't timeout while button is pressed - might become click+hold
        if (pendingClick && !pressed && (millis() - lastClickTime) >= config.doubleClickWindowMs) {
            pendingClick = false;
            emit(Action::SINGLE_CLICK);
        }
    }

private:
    void emit(Action a) {
        if (callback) callback(a);
    }

    bool pressed = false;
    bool pendingClick = false;
    bool hadClickBeforeHold = false;
    uint32_t lastClickTime = 0;
    Callback callback = nullptr;
    Config config;
};

#endif // GESTURE_DETECTOR_H