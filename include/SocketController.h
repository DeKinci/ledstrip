#ifndef SOCKET_CONTROLLER_H
#define SOCKET_CONTROLLER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "Anime.h"
#include "EditAnimationListener.h"

class SocketController : public SelectAnimationListener, public EditAnimationListener {
private:
    AsyncWebSocket *ws;

    void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
    void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);

public:
    SocketController();
    virtual ~SocketController();

    void bind(AsyncWebServer &server);
    void cleanUp();

    void textAll(String text);

    void animationSelected(String name);
    void animationAdded(String name);
    void animationRemoved(String name);
};


#endif //SOCKET_CONTROLLER_H