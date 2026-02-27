#include <Arduino.h>
#include <FastLED.h>

#ifndef LED_PIN
#define LED_PIN 4
#endif

#ifndef LED_COUNT
#define LED_COUNT 1
#endif

#ifndef LED_MODEL
#define LED_MODEL WS2811
#endif

#ifndef RGB_ORDER
#define RGB_ORDER RGB
#endif

CRGB leds[LED_COUNT];

void setup() {
    Serial.begin(115200);
    delay(1000);
    FastLED.addLeds<LED_MODEL, LED_PIN, RGB_ORDER>(leds, LED_COUNT);
    FastLED.setBrightness(255);
}

void loop() {
    leds[0] = CRGB::Red;
    FastLED.show();
    Serial.println("RED");
    delay(1000);
}
