#pragma once

// Seeed XIAO ESP32-S3 board pin to GPIO mapping.
// Use these instead of raw GPIO numbers for readable pin assignments.
//
// Usage:
//   #include <XiaoPins.h>
//   #define LORA_TX_PIN XIAO_D3

#define XIAO_D0  1   // A0, Touch, ADC
#define XIAO_D1  2   // A1, Touch, ADC
#define XIAO_D2  3   // A2, Touch, ADC
#define XIAO_D3  4   // A3, Touch, ADC
#define XIAO_D4  5   // A4, I2C SDA, Touch, ADC
#define XIAO_D5  6   // A5, I2C SCL, Touch, ADC
#define XIAO_D6  43  // UART TX
#define XIAO_D7  44  // UART RX
#define XIAO_D8  7   // A8, SPI SCK, Touch, ADC
#define XIAO_D9  8   // A9, SPI MISO, Touch, ADC
#define XIAO_D10 9   // A10, SPI MOSI, Touch, ADC
