#include <Arduino.h>
#include "config.h"
#include "lora.h"
#include "relay.h"

static LoRa lora;
static Relay relay(lora);

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.printf("\n[Retranslator] Device ID: 0x%02X, Type: %s\n",
        DEVICE_ID, NODE_TYPE ? "Abonent" : "Retranslator");

    lora.begin();
    relay.begin();

    Serial.println("[Retranslator] Ready");
}

void loop() {
    relay.process();
    delay(10);
}
