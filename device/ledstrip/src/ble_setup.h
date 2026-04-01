#pragma once

#include <BleMan.h>
#include <BleButtonDriver.h>
#include "animations/Anime.h"

// Device-specific BLE driver registration.
// Keeps driver wiring out of main.cpp.
namespace BleSetup {

inline void init(BleMan::BleManager& mgr) {
    // "button" driver: BLE HID buttons that control animations
    mgr.registerDriver("button", [](const BleKnownDevice& dev) -> BleDriver* {
        auto* btn = BleButtonDriver::allocate();
        if (!btn) return nullptr;

        btn->setActionCallback([](SequenceDetector::Action action) {
            switch (action) {
                case SequenceDetector::Action::SINGLE_CLICK:
                    Anime::nextAnimation();
                    break;
                case SequenceDetector::Action::DOUBLE_CLICK:
                    Anime::previousAnimation();
                    break;
                case SequenceDetector::Action::HOLD_TICK:
                    Anime::setBrightness(min(255, Anime::getBrightness() + 3));
                    break;
                case SequenceDetector::Action::CLICK_HOLD_TICK:
                    Anime::setBrightness(max(0, Anime::getBrightness() - 3));
                    break;
                default:
                    break;
            }
        });

        return btn;
    });
}

} // namespace BleSetup
