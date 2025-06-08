#ifndef SOCKET_CONTROLLER_H
#define SOCKET_CONTROLLER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <FastLED.h>

namespace SocketController {
void bind(AsyncWebServer &server);
void cleanUp();

void animationSelected(String name);
void animationAdded(String name);
void animationRemoved(String name);
void updateLedVals(CRGB* leds, size_t actualLength);

};  // namespace SocketController

#endif  // SOCKET_CONTROLLER_H