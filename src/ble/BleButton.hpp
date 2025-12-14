#ifndef GARLAND_BLE_BUTTON_H
#define GARLAND_BLE_BUTTON_H

#include <NimBLEDevice.h>

class BleButton : public NimBLEClientCallbacks {
   public:
    // Legacy constructor - connects to a device by address
    BleButton(NimBLEAddress foundAddr);

    // New constructor - uses an already connected client
    BleButton(NimBLEClient* client);

    void loop();

    void onConnect(NimBLEClient* client) override;
    void onAuthenticationComplete(NimBLEConnInfo& connInfo) override;
    void onDisconnect(NimBLEClient* client, int reason) override;

   private:
    void initialize();

    NimBLEClient* connectedClient = nullptr;
    bool shouldSubscribe = false;
    bool managedClient = false;  // true if we created the client (legacy mode)
};

#endif