#ifndef GARLAND_BLE_CLIENT_H
#define GARLAND_BLE_CLIENT_H

#include <Arduino.h>
#include <NimBLEDevice.h>

struct BleDevice {
    String icon;
    String address;
    String name;
};

namespace BleClient {
    void init();
    void scan();
    void stopScan();

    void loop();
}

#endif