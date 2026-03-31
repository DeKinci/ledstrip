#include <MicroLog.h>
#include "MicroProtoServer.h"
#include "../MicroProtoController.h"
#include <Arduino.h>

namespace MicroProto {

void MicroProtoServer::begin(MicroProtoController* controller) {
    _controller = controller;
    _clientIdOffset = controller->registerTransport(this);

    _ws.begin();
    _ws.onEvent([this](uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
        handleEvent(num, type, payload, length);
    });

    LOG_INFO("Proto", "WebSocket server started");
}

void MicroProtoServer::loop() {
    _ws.loop();
}

uint8_t MicroProtoServer::connectedClients() {
    return _ws.connectedClients();
}

void MicroProtoServer::send(uint8_t localClientId, const uint8_t* data, size_t len) {
    _ws.sendBIN(localClientId, data, len);
}

void MicroProtoServer::handleEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    uint8_t globalId = num + _clientIdOffset;

    switch (type) {
        case WStype_CONNECTED:
            _controller->onClientConnected(globalId);
            break;

        case WStype_DISCONNECTED:
            _controller->onClientDisconnected(globalId);
            break;

        case WStype_BIN:
            _controller->processMessage(globalId, payload, length);
            break;

        case WStype_TEXT:
            LOG_WARN("Proto", "Unexpected text from client %u", num);
            break;

        default:
            break;
    }
}

} // namespace MicroProto
