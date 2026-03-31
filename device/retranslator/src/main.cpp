#include <Arduino.h>
#include "config.h"
#include "lora.h"
#include "ble.h"
#include "relay.h"

static LoRa lora;
static BLE ble;
static Relay relay(lora, ble);

static void onBleReceive(const uint8_t* data, size_t len) {
    relay.onBleReceive(data, len);
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.printf("\n[Retranslator] Device ID: 0x%02X, Type: %s\n",
        DEVICE_ID, NODE_TYPE ? "Abonent" : "Retranslator");

    lora.begin();
    ble.begin(onBleReceive);
    relay.begin();

    Serial.println("[Retranslator] Ready");
}

void loop() {
    relay.process();
    delay(10);
}
