#ifndef SOCKET_CONTROLLER_H
#define SOCKET_CONTROLLER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

namespace SocketController {
void bind(AsyncWebServer &server);
void cleanUp();

void animationSelected(String name);
void animationAdded(String name);
void animationRemoved(String name);

};  // namespace SocketController

#endif  // SOCKET_CONTROLLER_H