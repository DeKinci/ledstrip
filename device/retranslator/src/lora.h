#pragma once

#include <cstdint>
#include <cstddef>

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
#endif
};
