#include "SocketController.h"

#include "Anime.h"

namespace {
AsyncWebSocket *ws = nullptr;

void textAll(String text) {
    if (ws != nullptr) {
        ws->textAll(text);
    }
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        data[len] = 0;
        String control = String((char *)data);
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

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
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
}  // namespace

namespace SocketController {

void bind(AsyncWebServer &server) {
    ws = new AsyncWebSocket("/control");
    ws->onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
        onEvent(server, client, type, arg, data, len);
    });
    server.addHandler(ws);
}

void cleanUp() { ws->cleanupClients(); }

void animationSelected(String name) { textAll("select " + name); }

void animationAdded(String name) { textAll("add " + name); }

void animationRemoved(String name) { textAll("delete " + name); }

}  // namespace SocketController
