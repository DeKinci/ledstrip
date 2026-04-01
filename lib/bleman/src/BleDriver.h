#ifndef BLEMAN_BLE_DRIVER_H
#define BLEMAN_BLE_DRIVER_H

#include <NimBLEDevice.h>

// Forward declaration
struct BleKnownDevice;

// Abstract driver interface for BLE peripherals.
// Each driver type handles a specific kind of device (button, sensor, etc.).
// Drivers are created from static pools — no heap allocation.
class BleDriver {
public:
    virtual ~BleDriver() = default;

    // Called when the peripheral connects. Return false to reject.
    virtual bool init(NimBLEClient* client) = 0;

    // Called every loop iteration while connected.
    virtual void loop() = 0;

    // Called on disconnect. Driver should reset and return itself to its pool.
    virtual void deinit() = 0;
};

// Factory function: returns a driver from a static pool, or nullptr if full.
// The BleKnownDevice is provided for context (name, type, address, etc.).
using BleDriverFactory = BleDriver*(*)(const BleKnownDevice& device);

#endif // BLEMAN_BLE_DRIVER_H
