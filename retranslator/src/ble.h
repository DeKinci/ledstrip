#pragma once

#include <cstdint>
#include <cstddef>

#ifndef NATIVE_TEST
#include <NimBLEDevice.h>
#endif

// Callback for data received from BLE client
using BleReceiveCallback = void(*)(const uint8_t* data, size_t len);

class BLE {
public:
    void begin(BleReceiveCallback onReceive);
    bool isConnected() const;
    bool send(const uint8_t* data, size_t len);

private:
    BleReceiveCallback _onReceive = nullptr;
    bool _connected = false;

#ifndef NATIVE_TEST
    NimBLECharacteristic* _txChar = nullptr;

    class ServerCallbacks;
    class RxCallbacks;
    friend class ServerCallbacks;
    friend class RxCallbacks;
#endif
};
