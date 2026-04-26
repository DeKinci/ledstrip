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

    // Read any available bytes into internal buffer
    while (_serial->available() > 0 && _rxLen < LORA_RX_BUF_SIZE) {
        _rxBuf[_rxLen++] = _serial->read();
        _lastByteMs = millis();
    }

    if (_rxLen == 0) return 0;

    // Packet complete when silence gap exceeded or buffer full
    if (millis() - _lastByteMs < LORA_PACKET_GAP_MS && _rxLen < LORA_RX_BUF_SIZE) {
        return 0;  // Still accumulating
    }

    // Return accumulated packet
    size_t toReturn = (_rxLen > maxLen) ? maxLen : _rxLen;
    memcpy(buf, _rxBuf, toReturn);
    _rxLen = 0;
    return toReturn;
}

#endif
