#include "BleButton.hpp"
#include "animations/Anime.h"

BleButton::BleButton(NimBLEAddress foundAddr) {
    NimBLEClient* client = NimBLEDevice::getDisconnectedClient();
    if (!client) {
        client = NimBLEDevice::createClient();
        if (!client) {
            Serial.println("Failed to create client");
            return;
        }
        client->setConnectionParams(12, 12, 0, 150);
        client->setConnectTimeout(5 * 1000);
    }

    client->setClientCallbacks(this, false);
    if (!client->connect(foundAddr, false, true, true)) {
        Serial.println("Connect failed");
        NimBLEDevice::deleteClient(client);
    }
}

void BleButton::onConnect(NimBLEClient* client) {
    Serial.printf("Connected: %s\n", client->getPeerAddress().toString().c_str());
    client->secureConnection(true);
    connectedClient = client;
}

void BleButton::onAuthenticationComplete(NimBLEConnInfo& connInfo) {
    Serial.printf("Auth: %d encryption %d\n", connInfo.isBonded(), connInfo.isEncrypted());
    shouldSubscribe = true;
}

void BleButton::onDisconnect(NimBLEClient* client, int reason) {
    Serial.printf("Disconnected (%s): reason = %d\n", client->getPeerAddress().toString().c_str(), reason);
    connectedClient = nullptr;
    shouldSubscribe = false;
}

String charId(NimBLERemoteCharacteristic* ch) {
    return "uuid: " + String(ch->getUUID().toString().c_str()) + " handle: 0x" + String(ch->getHandle(), HEX);
}

static const NimBLEUUID CHAR_UUID(uint16_t(0x2a4d));
static const NimBLEUUID SERVICE_UUID(uint16_t(0x1812));

void subscribeToClicks(NimBLERemoteService* service) {
    Serial.printf("Subscribing to clicks for service: %s\n", service->getUUID().toString().c_str());
    NimBLERemoteCharacteristic* inputChar = service->getCharacteristic(CHAR_UUID);
    for (auto* c : service->getCharacteristics()) {
        if (!c->canNotify()) {
            Serial.println("Input report characteristic not notifiable");
        } else {
            if (c->subscribe(
                    true,
                    [](NimBLERemoteCharacteristic* c, uint8_t* data, size_t len, bool notify) {
                        if (len > 0) {
                            if (data[0] == 0x2) {
                                Serial.println("Down!");
                            } else if (data[0] == 0) {
                                Serial.println("Up!");
                                Anime::nextAnimation();
                            } else {
                                Serial.println("Other!");
                            }
                        }
                    },
                    false)) {
                Serial.printf("Subscribed to %s\n", charId(c).c_str());
            } else {
                Serial.println("Subscribing failed");
            }
        }
    }
}

void BleButton::loop() {
    if (shouldSubscribe) {
        shouldSubscribe = false;
        NimBLERemoteService* pService = connectedClient->getService(SERVICE_UUID);
        if (pService != nullptr) {
            subscribeToClicks(pService);
        }
    }
}
