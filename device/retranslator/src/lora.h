#pragma once

#include <cstdint>
#include <cstddef>

#ifndef LORA_RX_BUF_SIZE
#define LORA_RX_BUF_SIZE 128
#endif

#ifndef LORA_PACKET_GAP_MS
#define LORA_PACKET_GAP_MS 5
#endif

#ifndef NATIVE_TEST
#include <HardwareSerial.h>
#endif

class LoRa {
public:
    void begin();
    bool send(const uint8_t* data, size_t len);
    size_t receive(uint8_t* buf, size_t maxLen);

private:
#ifndef NATIVE_TEST
    HardwareSerial* _serial = nullptr;

    // Non-blocking receive buffer — accumulates bytes across loop() calls,
    // returns complete packet after LORA_PACKET_GAP_MS silence gap.
    uint8_t _rxBuf[LORA_RX_BUF_SIZE] = {};
    size_t _rxLen = 0;
    uint32_t _lastByteMs = 0;
#endif
};
