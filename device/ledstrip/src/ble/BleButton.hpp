#ifndef GARLAND_BLE_BUTTON_H
#define GARLAND_BLE_BUTTON_H

#include <NimBLEDevice.h>

class BleButton {
   public:
    // Default constructor for static pool
    BleButton() = default;

    // Constructor - uses an already connected client from BleDeviceManager
    BleButton(NimBLEClient* client);

    void loop();

   private:
    NimBLEClient* connectedClient = nullptr;
    bool shouldSubscribe = false;
};

#endif