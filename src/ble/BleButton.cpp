#include "BleButton.hpp"

#include <Logger.h>

#include "input/GestureDetector.h"
#include "animations/Anime.h"

#define TAG "BTN"

namespace {

// Pending button events from BLE callback (runs on BLE task)
volatile bool pendingPress = false;
volatile bool pendingRelease = false;

// Gesture detection (runs on main task)
GestureDetector gestureDetector;
SequenceDetector sequenceDetector;

// Brightness ramping: 20% per second = 51/sec, at 20Hz = ~2.5 per tick
constexpr int BRIGHTNESS_STEP = 3;

void onSequenceAction(SequenceDetector::Action action) {
    switch (action) {
        case SequenceDetector::Action::SINGLE_CLICK:
            LOG_INFO(TAG, "Single click -> next");
            Anime::nextAnimation();
            break;

        case SequenceDetector::Action::DOUBLE_CLICK:
            LOG_INFO(TAG, "Double click -> prev");
            Anime::previousAnimation();
            break;

        case SequenceDetector::Action::HOLD_TICK: {
            int brightness = Anime::getBrightness();
            brightness = min(255, brightness + BRIGHTNESS_STEP);
            Anime::setBrightness(brightness);
            break;
        }

        case SequenceDetector::Action::CLICK_HOLD_TICK: {
            int brightness = Anime::getBrightness();
            brightness = max(0, brightness - BRIGHTNESS_STEP);
            Anime::setBrightness(brightness);
            break;
        }

        case SequenceDetector::Action::HOLD_END:
            LOG_INFO(TAG, "Hold end, brightness=%d", Anime::getBrightness());
            break;

        default:
            break;
    }
}

void onBasicGesture(BasicGesture gesture) {
    sequenceDetector.onGesture(gesture);
}

void initGestureDetection() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    gestureDetector.setCallback(onBasicGesture);
    sequenceDetector.setCallback(onSequenceAction);
}

}

// Constructor - uses an already connected client from BleDeviceManager
BleButton::BleButton(NimBLEClient* client) : connectedClient(client) {
    if (client && client->isConnected()) {
        // Client is already connected, just need to subscribe
        shouldSubscribe = true;
    }
}

String charId(NimBLERemoteCharacteristic* ch) {
    return "uuid: " + String(ch->getUUID().toString().c_str()) + " handle: 0x" + String(ch->getHandle(), HEX);
}

static const NimBLEUUID SERVICE_UUID(uint16_t(0x1812));  // HID service

void subscribeToClicks(NimBLERemoteService* service) {
    LOG_INFO(TAG, "Subscribing to service: %s", service->getUUID().toString().c_str());
    const auto& chars = service->getCharacteristics(true);  // true = refresh/discover from device
    LOG_INFO(TAG, "Found %d characteristics", chars.size());
    for (auto* c : chars) {
        if (!c->canNotify()) {
            LOG_DEBUG(TAG, "Characteristic not notifiable");
        } else {
            if (c->subscribe(
                    true,
                    [](NimBLERemoteCharacteristic* c, uint8_t* data, size_t len, bool notify) {
                        if (len > 0) {
                            // Most HID buttons: non-zero = press, zero = release
                            bool anyNonZero = false;
                            for (size_t i = 0; i < len; i++) {
                                if (data[i] != 0) anyNonZero = true;
                            }

                            // Queue for main loop (BLE callback runs on BLE task)
                            if (anyNonZero) {
                                pendingPress = true;
                            } else {
                                pendingRelease = true;
                            }
                        }
                    },
                    false)) {
                LOG_INFO(TAG, "Subscribed to %s", charId(c).c_str());
            } else {
                LOG_WARN(TAG, "Subscribe failed");
            }
        }
    }
}

void BleButton::loop() {
    initGestureDetection();

    if (shouldSubscribe && connectedClient != nullptr) {
        shouldSubscribe = false;
        LOG_INFO(TAG, "Getting HID service...");
        NimBLERemoteService* pService = connectedClient->getService(SERVICE_UUID);
        if (pService != nullptr) {
            LOG_INFO(TAG, "Got service, subscribing...");
            subscribeToClicks(pService);
        } else {
            LOG_WARN(TAG, "HID service not found!");
        }
    }

    // Process deferred button events (set from BLE task callback)
    if (pendingPress) {
        pendingPress = false;
        gestureDetector.onPress();
        sequenceDetector.onPress();
    }
    if (pendingRelease) {
        pendingRelease = false;
        gestureDetector.onRelease();
        sequenceDetector.onRelease();
    }

    // Run gesture detection state machines
    gestureDetector.loop();
    sequenceDetector.loop();
}
