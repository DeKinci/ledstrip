#include "MicroProtoServer.h"
#include <Arduino.h>

namespace MicroProto {

void MicroProtoServer::begin() {
    _ws.begin();
    _ws.onEvent([this](uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
        handleEvent(num, type, payload, length);
    });

    PropertySystem::onFlush([this](const DirtySet& dirty) {
        this->onPropertiesChanged(dirty);
    });

    Serial.println("[MicroProto] Server started");
}

void MicroProtoServer::loop() {
    _ws.loop();
    flushBroadcasts();
}

uint8_t MicroProtoServer::connectedClients() {
    return _ws.connectedClients();
}

void MicroProtoServer::broadcastProperty(const PropertyBase* prop) {
    if (_ws.connectedClients() == 0) return;

    uint8_t buf[TX_BUFFER_SIZE];
    WriteBuffer wb(buf, sizeof(buf));

    if (PropertyUpdate::encodeShort(wb, prop)) {
        _ws.broadcastBIN(buf, wb.position());
    }
}

void MicroProtoServer::broadcastAllProperties() {
    if (_ws.connectedClients() == 0) return;

    uint8_t buf[TX_BUFFER_SIZE];
    WriteBuffer wb(buf, sizeof(buf));

    if (PropertyEncoder::encodeAllValues(wb) > 0) {
        _ws.broadcastBIN(buf, wb.position());
    }
}

void MicroProtoServer::onHello(const HelloRequest& hello) {
    Serial.printf("[MicroProto] HELLO from device 0x%08lX, version %d\n",
                  static_cast<unsigned long>(hello.deviceId), hello.protocolVersion);

    if (hello.protocolVersion != PROTOCOL_VERSION) {
        sendError(_currentClient, ErrorMessage::protocolVersionMismatch());
        return;
    }

    sendHelloResponse(_currentClient);
    sendSchema(_currentClient);
    sendAllPropertyValues(_currentClient);

    _clientReady[_currentClient] = true;
    Serial.printf("[MicroProto] Client %u sync complete\n", _currentClient);
}

void MicroProtoServer::onPropertyUpdate(uint8_t propertyId, const void* value, size_t size) {
    for (PropertyBase* p = PropertyBase::head; p; p = p->next) {
        if (p->id == propertyId) {
            if (p->readonly) {
                Serial.printf("[MicroProto] Rejected write to readonly prop %d\n", propertyId);
                return;
            }

            p->setData(value, size);

            Serial.printf("[MicroProto] Property %d updated by client %u\n",
                          propertyId, _currentClient);

            broadcastPropertyExcept(p, _currentClient);
            return;
        }
    }

    Serial.printf("[MicroProto] Unknown property ID: %d\n", propertyId);
}

void MicroProtoServer::onError(const ErrorMessage& error) {
    Serial.printf("[MicroProto] Error from client: code=%d\n",
                  static_cast<uint16_t>(error.code));
}

void MicroProtoServer::onPing(uint32_t payload) {
    sendPong(_currentClient, payload);
}

void MicroProtoServer::handleEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            Serial.printf("[MicroProto] Client %u disconnected\n", num);
            if (num < MAX_CLIENTS) {
                _clientReady[num] = false;
            }
            break;

        case WStype_CONNECTED: {
            IPAddress ip = _ws.remoteIP(num);
            Serial.printf("[MicroProto] Client %u connected from %s\n",
                          num, ip.toString().c_str());
            if (num < MAX_CLIENTS) {
                _clientReady[num] = false;
            }
        } break;

        case WStype_BIN:
            _currentClient = num;
            if (!_router.process(payload, length)) {
                Serial.printf("[MicroProto] Parse error from client %u\n", num);
                sendError(num, ErrorMessage(ErrorCode::INVALID_OPCODE, "Parse error"));
            }
            break;

        case WStype_TEXT:
            Serial.printf("[MicroProto] Unexpected text from client %u\n", num);
            break;

        default:
            break;
    }
}

void MicroProtoServer::sendHelloResponse(uint8_t clientNum) {
    uint8_t buf[HelloResponse::encodedSize()];
    WriteBuffer wb(buf, sizeof(buf));

    HelloResponse response;
    response.protocolVersion = PROTOCOL_VERSION;
    response.maxPacketSize = TX_BUFFER_SIZE;
    response.sessionId = _nextSessionId++;
    response.serverTimestamp = millis() / 1000;

    if (response.encode(wb)) {
        _ws.sendBIN(clientNum, buf, wb.position());
    }
}

void MicroProtoServer::sendSchema(uint8_t clientNum) {
    uint8_t buf[TX_BUFFER_SIZE];
    WriteBuffer wb(buf, sizeof(buf));

    size_t count = SchemaEncoder::encodeAllProperties(wb);
    if (count > 0) {
        _ws.sendBIN(clientNum, buf, wb.position());
        Serial.printf("[MicroProto] Sent schema (%d properties) to client %u\n",
                      count, clientNum);
    }
}

void MicroProtoServer::sendAllPropertyValues(uint8_t clientNum) {
    uint8_t buf[TX_BUFFER_SIZE];
    WriteBuffer wb(buf, sizeof(buf));

    size_t count = PropertyEncoder::encodeAllValues(wb);
    if (count > 0) {
        _ws.sendBIN(clientNum, buf, wb.position());
        Serial.printf("[MicroProto] Sent %d property values to client %u\n",
                      count, clientNum);
    }
}

void MicroProtoServer::sendError(uint8_t clientNum, const ErrorMessage& error) {
    uint8_t buf[128];
    WriteBuffer wb(buf, sizeof(buf));

    if (error.encode(wb)) {
        _ws.sendBIN(clientNum, buf, wb.position());
    }
}

void MicroProtoServer::sendPong(uint8_t clientNum, uint32_t payload) {
    uint8_t buf[5];
    WriteBuffer wb(buf, sizeof(buf));

    OpHeader header(OpCode::PONG);
    wb.writeByte(header.encode());
    wb.writeUint32(payload);

    Serial.printf("[MicroProto] Sending PONG to client %u, payload=%lu\n",
                  clientNum, static_cast<unsigned long>(payload));
    _ws.sendBIN(clientNum, buf, wb.position());
}

void MicroProtoServer::broadcastPropertyExcept(const PropertyBase* prop, uint8_t excludeClient) {
    uint8_t buf[TX_BUFFER_SIZE];
    WriteBuffer wb(buf, sizeof(buf));

    if (!PropertyUpdate::encodeShort(wb, prop)) return;

    for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
        if (i != excludeClient && _clientReady[i]) {
            _ws.sendBIN(i, buf, wb.position());
        }
    }
}

void MicroProtoServer::onPropertiesChanged(const DirtySet& dirty) {
    for (PropertyBase* p = PropertyBase::head; p; p = p->next) {
        if (dirty.test(p->id)) {
            _pendingBroadcast.set(p->id);
        }
    }
}

void MicroProtoServer::flushBroadcasts() {
    if (!_pendingBroadcast.any()) return;

    if (_ws.connectedClients() == 0) {
        _pendingBroadcast.clearAll();
        return;
    }

    uint32_t now = millis();
    if (now - _lastBroadcastTime < BROADCAST_INTERVAL_MS) return;
    _lastBroadcastTime = now;

    uint8_t buf[TX_BUFFER_SIZE];
    WriteBuffer wb(buf, sizeof(buf));

    for (PropertyBase* p = PropertyBase::head; p; p = p->next) {
        if (_pendingBroadcast.test(p->id)) {
            PropertyUpdate::encodeShort(wb, p);
        }
    }

    if (wb.position() > 0) {
        _ws.broadcastBIN(buf, wb.position());
    }

    _pendingBroadcast.clearAll();
}

} // namespace MicroProto
