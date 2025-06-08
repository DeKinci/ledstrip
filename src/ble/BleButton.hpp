#ifndef GARLAND_BLE_BUTTON_H
#define GARLAND_BLE_BUTTON_H

#include <NimBLEDevice.h>

class BleButton : public NimBLEClientCallbacks {
   public:
    BleButton(NimBLEAddress foundAddr);

    void loop();

    void onConnect(NimBLEClient* client) override;
    void onAuthenticationComplete(NimBLEConnInfo& connInfo) override;
    void onDisconnect(NimBLEClient* client, int reason) override;

   private:
    NimBLEClient* connectedClient = nullptr;
    bool shouldSubscribe = false;
};

#endif