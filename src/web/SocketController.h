#ifndef SOCKET_CONTROLLER_H
#define SOCKET_CONTROLLER_H

#include <Arduino.h>
#include <WebSocketsServer.h>
#include <FastLED.h>

namespace SocketController {
void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
void setWebSocket(WebSocketsServer* ws);

void animationSelected(const String& name);
void animationAdded(const String& name);
void animationRemoved(const String& name);
void updateLedVals(CRGB* leds, size_t actualLength);

};  // namespace SocketController

#endif  // SOCKET_CONTROLLER_H