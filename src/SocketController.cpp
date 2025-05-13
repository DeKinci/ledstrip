#include "SocketController.h"

SocketController::SocketController() {
    ws = new AsyncWebSocket("/control");
    ws->onEvent([this](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
        this->onEvent(server, client, type, arg, data, len);
    });
}

SocketController::~SocketController() {
    delete ws;
}

void SocketController::bind(AsyncWebServer &server) {
    server.addHandler(ws);
}

void SocketController::onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
        Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
        break;
        case WS_EVT_DISCONNECT:
        Serial.printf("WebSocket client #%u disconnected\n", client->id());
        break;
        case WS_EVT_DATA:
        handleWebSocketMessage(arg, data, len);
        break;
        case WS_EVT_PONG:
        break;
        case WS_EVT_ERROR:
        break;
  }
}

void SocketController::handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        data[len] = 0;
        String control = String((char*) data);
        Serial.println("Control sequence: " + control);
        if (control.startsWith("select ")) {
            String shaderName = control.substring(7);
            Anime::select(shaderName);
            textAll(control);
        } else if (control.startsWith("limitLeds ")) {
            int limitTo = control.substring(10).toInt();
            Anime::setCurrentLeds(limitTo);
            textAll(control);
        }
    }
}

void SocketController::cleanUp() {
    ws->cleanupClients();
}

void SocketController::textAll(String text) {
    ws->textAll(text);
}

void SocketController::animationSelected(String name) {
    ws->textAll("select " + name);
}

void SocketController::animationAdded(String name) {
    ws->textAll("add " + name);
}

void SocketController::animationRemoved(String name) {
    ws->textAll("delete " + name);
}

