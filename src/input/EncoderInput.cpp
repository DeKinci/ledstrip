#include "EncoderInput.hpp"
#include "animations/Anime.h"

namespace {

// Rotary Encoder Pins
#define CLK 4
#define DT 2
#define SW 3

volatile int encoderPos = 0;
volatile bool encoderChanged = false;
unsigned long lastButtonPress = 0;
unsigned long lastEncoderUpdate = 0;

static uint8_t prevState = 0;

void IRAM_ATTR encoderISR() {
    // Read both pins
    uint8_t clkState = digitalRead(CLK);
    uint8_t dtState = digitalRead(DT);

    // Create current state from both pins (2 bits: CLK=bit1, DT=bit0)
    uint8_t currentState = (clkState << 1) | dtState;

    // Quadrature encoding state machine
    // Valid transitions: 00->01->11->10->00 (CW) or reverse (CCW)
    uint8_t combined = (prevState << 2) | currentState;

    switch(combined) {
        case 0b0001: case 0b0111: case 0b1110: case 0b1000: // CW steps
            encoderPos++;
            encoderChanged = true;
            break;
        case 0b0010: case 0b1011: case 0b1101: case 0b0100: // CCW steps
            encoderPos--;
            encoderChanged = true;
            break;
    }

    prevState = currentState;
}

}  // namespace

namespace EncoderInput {

void init() {
    pinMode(CLK, INPUT_PULLUP);
    pinMode(DT, INPUT_PULLUP);
    pinMode(SW, INPUT_PULLUP);

    // Initialize previous state
    prevState = (digitalRead(CLK) << 1) | digitalRead(DT);

    // Attach interrupts to both pins for proper quadrature decoding
    attachInterrupt(digitalPinToInterrupt(CLK), encoderISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(DT), encoderISR, CHANGE);

    Serial.println("EncoderInput initialized with interrupts");
}

void loop() {
    // Check if encoder changed and enough time has passed (debouncing)
    if (encoderChanged && (millis() - lastEncoderUpdate > 50)) {
        encoderChanged = false;
        lastEncoderUpdate = millis();

        uint8_t brightness = Anime::getBrightness();
        int direction = encoderPos;
        encoderPos = 0;  // Reset position counter

        if (direction > 0) {
            // CW rotation - increase brightness
            if (brightness <= 245) {
                brightness += 10;
            } else {
                brightness = 255;
            }
            Serial.printf("Brightness UP: %d\n", brightness);
        } else if (direction < 0) {
            // CCW rotation - decrease brightness
            if (brightness >= 10) {
                brightness -= 10;
            } else {
                brightness = 0;
            }
            Serial.printf("Brightness DOWN: %d\n", brightness);
        }

        Anime::setBrightness(brightness);
    }

    // Read the button state
    int btnState = digitalRead(SW);

    // If we detect LOW signal, button is pressed
    if (btnState == LOW) {
        // if 50ms have passed since last LOW pulse, it means that the
        // button has been pressed, released and pressed again
        if (millis() - lastButtonPress > 50) {
            Anime::nextAnimation();
            Serial.println("Button pressed - next animation");
        }

        // Remember last button press event
        lastButtonPress = millis();
    }

    // Small delay
    delay(1);
}

}  // namespace EncoderInput
