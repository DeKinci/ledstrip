#include "BleButton.hpp"
#include "animations/Anime.h"
#include <Logger.h>

#define TAG "BTN"

namespace {
// Deferred action flag - set from BLE task callback, processed in loop()
// Using int8_t: 0 = none, 1 = next, -1 = previous
volatile int8_t pendingAction = 0;
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
                        // Format HID data as hex string
                        char hexBuf[64];
                        size_t pos = 0;
                        for (size_t i = 0; i < len && pos < sizeof(hexBuf) - 4; i++) {
                            pos += snprintf(hexBuf + pos, sizeof(hexBuf) - pos, " %02X", data[i]);
                        }
                        hexBuf[pos] = '\0';
                        LOG_DEBUG("HID", "len=%d data:%s", len, hexBuf);

                        if (len > 0) {
                            // Most HID buttons: non-zero = press, zero = release
                            bool anyNonZero = false;
                            for (size_t i = 0; i < len; i++) {
                                if (data[i] != 0) anyNonZero = true;
                            }

                            if (anyNonZero) {
                                LOG_INFO("BTN", "Press");
                            } else {
                                LOG_INFO("BTN", "Release");
                                // Queue action for processing in main loop
                                pendingAction = 1;
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

    // Process deferred button actions (set from BLE task callback)
    int8_t action = pendingAction;
    if (action != 0) {
        pendingAction = 0;
        if (action > 0) {
            Anime::nextAnimation();
        } else {
            Anime::previousAnimation();
        }
    }
}
