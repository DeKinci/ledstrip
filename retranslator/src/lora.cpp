#ifndef NATIVE_TEST

#include "lora.h"
#include "config.h"
#include <Arduino.h>

void LoRa::begin() {
    // Configure M0/M1 for transparent transmission mode (both LOW)
    pinMode(LORA_M0_PIN, OUTPUT);
    pinMode(LORA_M1_PIN, OUTPUT);
    digitalWrite(LORA_M0_PIN, LOW);
    digitalWrite(LORA_M1_PIN, LOW);

    if (LORA_AUX_PIN >= 0) {
        pinMode(LORA_AUX_PIN, INPUT);
    }

    _serial = &Serial1;
    _serial->begin(LORA_BAUD_RATE, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);

    // Wait for module ready (AUX goes HIGH)
    if (LORA_AUX_PIN >= 0) {
        uint32_t start = millis();
        while (digitalRead(LORA_AUX_PIN) == LOW && millis() - start < 1000) {
            delay(10);
        }
    } else {
        delay(100);
    }

    Serial.println("[LoRa] Initialized in transparent mode");
}

bool LoRa::send(const uint8_t* data, size_t len) {
    if (!_serial || len == 0) return false;

    // Wait for AUX HIGH (module ready to accept data)
    if (LORA_AUX_PIN >= 0) {
        uint32_t start = millis();
        while (digitalRead(LORA_AUX_PIN) == LOW && millis() - start < 500) {
            delay(1);
        }
    }

    size_t written = _serial->write(data, len);
    _serial->flush();
    return written == len;
}

size_t LoRa::receive(uint8_t* buf, size_t maxLen) {
    if (!_serial) return 0;

    size_t available = _serial->available();
    if (available == 0) return 0;

    // Wait briefly for full packet to arrive
    delay(5);
    available = _serial->available();

    size_t toRead = (available > maxLen) ? maxLen : available;
    return _serial->readBytes(buf, toRead);
}

#endif
