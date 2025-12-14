#include "SocketController.h"
#include "animations/Anime.h"

namespace SocketController {

static WebSocketsServer* wsServer = nullptr;

void setWebSocket(WebSocketsServer* ws) {
    wsServer = ws;
}

void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.printf("WebSocket client %u disconnected\n", num);
            break;

        case WStype_CONNECTED:
            {
                if (wsServer) {
                    IPAddress ip = wsServer->remoteIP(num);
                    Serial.printf("WebSocket client %u connected from %s\n", num, ip.toString().c_str());
                }
            }
            break;

        case WStype_TEXT:
            {
                String control = String((char*)payload);
                Serial.println("Control: " + control);

                if (control.startsWith("select ")) {
                    String shaderName = control.substring(7);
                    Anime::select(shaderName);
                    if (wsServer) {
                        wsServer->broadcastTXT(control);
                    }
                } else if (control.startsWith("limitLeds ")) {
                    int limitTo = control.substring(10).toInt();
                    Anime::setCurrentLeds(limitTo);
                    if (wsServer) {
                        wsServer->broadcastTXT(control);
                    }
                }
            }
            break;

        case WStype_BIN:
        case WStype_PING:
        case WStype_PONG:
        case WStype_ERROR:
        case WStype_FRAGMENT_TEXT_START:
        case WStype_FRAGMENT_BIN_START:
        case WStype_FRAGMENT:
        case WStype_FRAGMENT_FIN:
            break;
    }
}

void animationSelected(const String& name) {
    if (!wsServer || wsServer->connectedClients() == 0) return;

    String msg = "select " + name;
    wsServer->broadcastTXT(msg);
}

void animationAdded(const String& name) {
    if (!wsServer || wsServer->connectedClients() == 0) return;

    String msg = "add " + name;
    wsServer->broadcastTXT(msg);
}

void animationRemoved(const String& name) {
    if (!wsServer || wsServer->connectedClients() == 0) return;

    String msg = "delete " + name;
    wsServer->broadcastTXT(msg);
}

void updateLedVals(CRGB* leds, size_t actualLength) {
    if (!wsServer || wsServer->connectedClients() == 0) return;

    // Format: [0x01, R, G, B, R, G, B, ...]
    size_t msgSize = 1 + (actualLength * 3);
    uint8_t* msg = new uint8_t[msgSize];
    msg[0] = 0x01;  // Binary LED data marker

    for (size_t i = 0; i < actualLength; i++) {
        msg[1 + i*3] = leds[i].r;
        msg[2 + i*3] = leds[i].g;
        msg[3 + i*3] = leds[i].b;
    }

    wsServer->broadcastBIN(msg, msgSize);
    delete[] msg;
}

};  // namespace SocketController
