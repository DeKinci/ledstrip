#ifndef BLEMAN_BLE_BUTTON_DRIVER_H
#define BLEMAN_BLE_BUTTON_DRIVER_H

#include "BleDriver.h"
#include <GestureDetector.h>
#include <MicroFunction.h>
#include <array>

#ifndef BLEMAN_MAX_CONNECTED_DEVICES
#define BLEMAN_MAX_CONNECTED_DEVICES 4
#endif

// Built-in BLE HID button driver.
// Subscribes to HID notifications, runs gesture/sequence detection,
// emits high-level actions via per-instance callback.
//
// Usage (in device factory):
//   auto* btn = BleButtonDriver::allocate();
//   btn->setActionCallback([](SequenceDetector::Action a) { ... });
//   return btn;
class BleButtonDriver : public BleDriver {
public:
    using ActionCallback = microcore::MicroFunction<void(SequenceDetector::Action), 8>;

    BleButtonDriver() = default;

    // Set per-instance action callback (call before init)
    void setActionCallback(ActionCallback cb) { _onAction = cb; }

    // Set the service UUID to subscribe to (default: HID 0x1812)
    void setServiceUUID(const char* uuid) { _serviceUUID = uuid; }

    // Get an instance from the static pool. Returns nullptr if full.
    static BleButtonDriver* allocate();

    // BleDriver interface
    bool init(NimBLEClient* client) override;
    void loop() override;
    void deinit() override;

private:
    NimBLEClient* _client = nullptr;
    const char* _serviceUUID = nullptr;  // null = default HID 0x1812
    bool _needsSubscribe = false;
    bool _inUse = false;

    GestureDetector _gesture;
    SequenceDetector _sequence;
    ActionCallback _onAction;

    // Pending events from BLE task (cross-task communication)
    volatile bool _pendingPress = false;
    volatile bool _pendingRelease = false;

    void subscribeToHID();
    int poolIndex() const;

    static std::array<BleButtonDriver, BLEMAN_MAX_CONNECTED_DEVICES> _pool;
};

#endif // BLEMAN_BLE_BUTTON_DRIVER_H
