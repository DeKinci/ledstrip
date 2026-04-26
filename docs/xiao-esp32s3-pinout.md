# Seeed XIAO ESP32-S3 Pinout

Source: [Seeed Studio Wiki](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/)

## Pin-to-GPIO Mapping

| Board Pin | GPIO | Default Function | Capabilities |
|-----------|------|------------------|--------------|
| D0 / A0   | GPIO1  | General IO | Touch, ADC |
| D1 / A1   | GPIO2  | General IO | Touch, ADC |
| D2 / A2   | GPIO3  | General IO | Touch, ADC |
| D3 / A3   | GPIO4  | General IO | Touch, ADC |
| D4 / A4   | GPIO5  | I2C SDA    | Touch, ADC |
| D5 / A5   | GPIO6  | I2C SCL    | Touch, ADC |
| D6        | GPIO43 | UART TX    | — |
| D7        | GPIO44 | UART RX    | — |
| D8 / A8   | GPIO7  | SPI SCK    | Touch, ADC |
| D9 / A9   | GPIO8  | SPI MISO   | Touch, ADC |
| D10 / A10 | GPIO9  | SPI MOSI   | Touch, ADC |

All GPIO pins support PWM output.

## Default Bus Assignments

| Bus  | Signal | Board Pin | GPIO |
|------|--------|-----------|------|
| I2C  | SDA    | D4        | GPIO5 |
| I2C  | SCL    | D5        | GPIO6 |
| SPI  | SCK    | D8        | GPIO7 |
| SPI  | MISO   | D9        | GPIO8 |
| SPI  | MOSI   | D10       | GPIO9 |
| UART | TX     | D6        | GPIO43 |
| UART | RX     | D7        | GPIO44 |

## Special Pins

| Function    | GPIO   | Notes |
|-------------|--------|-------|
| USER_LED    | GPIO21 | PWM capable, active low |
| Boot button | GPIO0  | Hold during reset to enter bootloader |

