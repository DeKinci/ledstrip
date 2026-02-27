#include "EncoderInput.hpp"
#include "animations/Anime.h"
#include <Logger.h>
#include <driver/gpio.h>

#if defined(SOC_PCNT_SUPPORTED) && SOC_PCNT_SUPPORTED
#include <ESP32Encoder.h>
#define USE_PCNT 1
#endif

static const char* TAG = "Encoder";

namespace {

// Rotary Encoder Pins
#define CLK 8
#define DT 9
#define SW 7

#ifdef USE_PCNT
ESP32Encoder encoder;
#else
// ISR-based fallback for chips without PCNT (e.g. ESP32-C3)
volatile int encoderPos = 0;
volatile bool encoderChanged = false;
static uint8_t prevState = 0;

void IRAM_ATTR encoderISR() {
    uint8_t clkState = digitalRead(CLK);
    uint8_t dtState = digitalRead(DT);
    uint8_t currentState = (clkState << 1) | dtState;
    uint8_t combined = (prevState << 2) | currentState;
    switch(combined) {
        case 0b0001: case 0b0111: case 0b1110: case 0b1000:
            encoderPos++; encoderChanged = true; break;
        case 0b0010: case 0b1011: case 0b1101: case 0b0100:
            encoderPos--; encoderChanged = true; break;
    }
    prevState = currentState;
}
#endif

volatile bool buttonPressed = false;

void IRAM_ATTR buttonISR() {
    gpio_intr_disable((gpio_num_t)SW);
    buttonPressed = true;
}

}  // namespace

namespace EncoderInput {

void init() {
    pinMode(SW, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(SW), buttonISR, FALLING);

#ifdef USE_PCNT
    ESP32Encoder::useInternalWeakPullResistors = puType::up;
    encoder.attachSingleEdge(DT, CLK);
    encoder.setFilter(1023);  // Max hardware glitch filter (~12.8µs)
    encoder.clearCount();
    LOG_INFO(TAG, "Initialized with PCNT encoder");
#else
    pinMode(CLK, INPUT_PULLUP);
    pinMode(DT, INPUT_PULLUP);
    prevState = (digitalRead(CLK) << 1) | digitalRead(DT);
    attachInterrupt(digitalPinToInterrupt(CLK), encoderISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(DT), encoderISR, CHANGE);
    LOG_INFO(TAG, "Initialized with ISR encoder (no PCNT)");
#endif

    // Configure GPIO wakeup for light sleep power saving
    gpio_wakeup_enable((gpio_num_t)CLK, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable((gpio_num_t)DT, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable((gpio_num_t)SW, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
}

void loop() {
    // --- Encoder rotation ---
#ifdef USE_PCNT
    int64_t count = encoder.getCount();
    if (count != 0) {
        encoder.clearCount();
        Anime::wakeUp();

        uint8_t brightness = Anime::getBrightness();
        if (count > 0) {
            brightness = (brightness <= 245) ? brightness + 10 : 255;
            LOG_INFO(TAG, "Brightness UP: %d", brightness);
        } else {
            brightness = (brightness >= 10) ? brightness - 10 : 0;
            LOG_INFO(TAG, "Brightness DOWN: %d", brightness);
        }
        Anime::setBrightness(brightness);
    }
#else
    if (encoderChanged) {
        encoderChanged = false;
        Anime::wakeUp();

        int direction = encoderPos;
        encoderPos = 0;

        uint8_t brightness = Anime::getBrightness();
        if (direction > 0) {
            brightness = (brightness <= 245) ? brightness + 10 : 255;
            LOG_INFO(TAG, "Brightness UP: %d", brightness);
        } else if (direction < 0) {
            brightness = (brightness >= 10) ? brightness - 10 : 0;
            LOG_INFO(TAG, "Brightness DOWN: %d", brightness);
        }
        Anime::setBrightness(brightness);
    }
#endif

    // --- Button press ---
    // ISR disables its own interrupt to kill bounce storm.
    // We process the press, then re-enable after 200ms debounce.
    static uint32_t buttonDisableTime = 0;

    if (buttonPressed) {
        buttonPressed = false;
        buttonDisableTime = millis();
        Anime::wakeUp();
        Anime::nextAnimation();
        LOG_INFO(TAG, "Button pressed - next animation");
    }

    if (buttonDisableTime > 0 && millis() - buttonDisableTime > 200) {
        buttonDisableTime = 0;
        gpio_intr_enable((gpio_num_t)SW);
    }
}

}  // namespace EncoderInput
