#include "MicroProtoServer.h"
#include <Arduino.h>

namespace MicroProto {

void MicroProtoServer::begin() {
    _ws.begin();
    _ws.onEvent([this](uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
        handleEvent(num, type, payload, length);
    });

    PropertySystem::addFlushListener(this);

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

    WriteBuffer wb(_txBuf, TX_BUFFER_SIZE);

    if (PropertyUpdate::encode(wb, prop)) {
        // Only send to clients that have completed handshake
        for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
            if (_clientReady[i]) {
                _ws.sendBIN(i, _txBuf, wb.position());
            }
        }
    }
}

void MicroProtoServer::broadcastAllProperties() {
    if (_ws.connectedClients() == 0) return;

    WriteBuffer wb(_txBuf, TX_BUFFER_SIZE);

    if (PropertyEncoder::encodeAllValues(wb) > 0) {
        // Only send to clients that have completed handshake
        for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
            if (_clientReady[i]) {
                _ws.sendBIN(i, _txBuf, wb.position());
            }
        }
    }
}

void MicroProtoServer::onHello(uint8_t clientId, const Hello& hello) {
    // We only handle requests (is_response=false) on server side
    if (hello.isResponse) {
        Serial.printf("[MicroProto] Unexpected HELLO response from client %u\n", clientId);
        return;
    }

    Serial.printf("[MicroProto] HELLO from device 0x%08lX, version %d\n",
                  static_cast<unsigned long>(hello.deviceId), hello.protocolVersion);

    if (hello.protocolVersion != PROTOCOL_VERSION) {
        sendError(clientId, ErrorMessage::protocolVersionMismatch());
        return;
    }

    // Clear ready state before resync — prevents stale broadcasts during handshake
    if (clientId < MAX_CLIENTS) {
        _clientReady[clientId] = false;
    }

    sendHelloResponse(clientId);
    sendSchema(clientId);
    sendAllPropertyValues(clientId);

    if (clientId < MAX_CLIENTS) {
        _clientReady[clientId] = true;
    }
    Serial.printf("[MicroProto] Client %u sync complete\n", clientId);
}

void MicroProtoServer::onPropertyUpdate(uint8_t clientId, uint16_t propertyId, const void* value, size_t size) {
    if (propertyId > 255) {
        Serial.printf("[MicroProto] Property ID %d exceeds MVP limit\n", propertyId);
        return;
    }

    PropertyBase* p = PropertyBase::find(static_cast<uint8_t>(propertyId));
    if (!p) {
        Serial.printf("[MicroProto] Unknown property ID: %d\n", propertyId);
        return;
    }

    if (p->readonly) {
        Serial.printf("[MicroProto] Rejected write to readonly prop %d\n", propertyId);
        return;
    }

    p->setData(value, size);

    Serial.printf("[MicroProto] Property %d updated by client %u\n",
                  propertyId, clientId);

    broadcastPropertyExcept(p, clientId);
}

void MicroProtoServer::onError(uint8_t clientId, const ErrorMessage& error) {
    Serial.printf("[MicroProto] Error from client %u: code=%d, schemaMismatch=%d\n",
                  clientId, static_cast<uint16_t>(error.code), error.schemaMismatch);
}

void MicroProtoServer::onPing(uint8_t clientId, bool isResponse, uint32_t payload) {
    if (isResponse) {
        // Unexpected pong from client (we didn't send a ping)
        Serial.printf("[MicroProto] Unexpected PONG from client %u\n", clientId);
        return;
    }
    // Client sent ping, respond with pong
    sendPong(clientId, payload);
}

void MicroProtoServer::onConstraintViolation(uint8_t clientId, uint16_t propertyId, ErrorCode code) {
    Serial.printf("[MicroProto] Constraint violation on property %d, sending error to client %u\n",
                  propertyId, clientId);
    sendError(clientId, ErrorMessage::validationFailed("Constraint violation"));
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
            if (!_router.process(num, payload, length)) {
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
    uint8_t buf[32];  // Variable size due to varint encoding
    WriteBuffer wb(buf, sizeof(buf));

    Hello response = Hello::response(_nextSessionId++, millis() / 1000, TX_BUFFER_SIZE);

    if (response.encode(wb)) {
        _ws.sendBIN(clientNum, buf, wb.position());
    }
}

void MicroProtoServer::sendSchema(uint8_t clientNum) {
    WriteBuffer wb(_txBuf, TX_BUFFER_SIZE);

    size_t count = SchemaEncoder::encodeAllProperties(wb);
    if (count > 0) {
        _ws.sendBIN(clientNum, _txBuf, wb.position());
        Serial.printf("[MicroProto] Sent schema (%d properties) to client %u\n",
                      count, clientNum);
    }
}

void MicroProtoServer::sendAllPropertyValues(uint8_t clientNum) {
    WriteBuffer wb(_txBuf, TX_BUFFER_SIZE);

    size_t count = PropertyEncoder::encodeAllValues(wb);
    if (count > 0) {
        _ws.sendBIN(clientNum, _txBuf, wb.position());
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
    uint8_t buf[16];
    WriteBuffer wb(buf, sizeof(buf));

    // PING opcode with is_response flag set
    wb.writeByte(encodeOpHeader(OpCode::PING, Flags::IS_RESPONSE));
    wb.writeVarint(payload);

    _ws.sendBIN(clientNum, buf, wb.position());
}

void MicroProtoServer::broadcastPropertyExcept(const PropertyBase* prop, uint8_t excludeClient) {
    WriteBuffer wb(_txBuf, TX_BUFFER_SIZE);

    if (!PropertyUpdate::encode(wb, prop)) return;

    for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
        if (i != excludeClient && _clientReady[i]) {
            _ws.sendBIN(i, _txBuf, wb.position());
        }
    }
}

void MicroProtoServer::onPropertiesChanged(const DirtySet& dirty) {
    for (uint8_t i = 0; i < PropertyBase::count; i++) {
        if (dirty.test(i)) {
            _pendingBroadcast.set(i);
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

    // Collect dirty properties into array for batched encoding
    const PropertyBase* dirtyProps[PropertyBase::MAX_PROPERTIES];
    size_t dirtyCount = 0;

    for (uint8_t i = 0; i < PropertyBase::count; i++) {
        if (_pendingBroadcast.test(i)) {
            PropertyBase* p = PropertyBase::byId[i];
            if (p) {
                dirtyProps[dirtyCount++] = p;
            }
        }
    }

    if (dirtyCount == 0) {
        _pendingBroadcast.clearAll();
        return;
    }

    // Encode in batches, flushing when buffer gets full
    size_t batchStart = 0;

    while (batchStart < dirtyCount) {
        WriteBuffer wb(_txBuf, TX_BUFFER_SIZE);

        // Try to fit as many properties as possible
        size_t batchEnd = batchStart;
        size_t lastGoodPos = 0;

        // Write batch header
        PropertyUpdateFlags flags;
        flags.batch = true;
        wb.writeByte(encodeOpHeader(OpCode::PROPERTY_UPDATE, flags.encode()));
        size_t countPos = wb.position();
        wb.writeByte(0);  // Placeholder for count-1

        while (batchEnd < dirtyCount) {
            size_t posBefore = wb.position();

            // Try encoding this property
            const PropertyBase* prop = dirtyProps[batchEnd];
            if (!wb.writePropId(prop->id)) break;

            if (!TypeCodec::encodeProperty(wb, prop)) {
                wb.setPosition(posBefore);
                break;
            }

            lastGoodPos = wb.position();
            batchEnd++;
        }

        size_t batchCount = batchEnd - batchStart;
        if (batchCount == 0) break;  // Can't fit even one property

        // Update count byte (count - 1)
        _txBuf[countPos] = static_cast<uint8_t>(batchCount - 1);

        // Send to all ready clients
        for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
            if (_clientReady[i]) {
                _ws.sendBIN(i, _txBuf, lastGoodPos);
            }
        }

        batchStart = batchEnd;
    }

    _pendingBroadcast.clearAll();
}

// =========== Resource Handlers ===========

void MicroProtoServer::onResourceGetRequest(uint8_t clientId, uint8_t requestId, uint16_t propertyId, uint32_t resourceId) {
    PropertyBase* prop = PropertyBase::find(static_cast<uint8_t>(propertyId));
    if (!prop || prop->getTypeId() != TYPE_RESOURCE) {
        Serial.printf("[MicroProto] RESOURCE_GET: property %d not found or not RESOURCE type\n", propertyId);
        sendResourceGetError(clientId, requestId, ResourceError::NOT_FOUND);
        return;
    }

    // Get body size first
    size_t bodySize = prop->getResourceBodySize(resourceId);
    if (bodySize == 0) {
        Serial.printf("[MicroProto] RESOURCE_GET: resource %lu not found in property %d\n", resourceId, propertyId);
        sendResourceGetError(clientId, requestId, ResourceError::NOT_FOUND);
        return;
    }

    // Cap body size to prevent unbounded allocation
    if (bodySize > TX_BUFFER_SIZE) {
        Serial.printf("[MicroProto] RESOURCE_GET: body %lu bytes exceeds TX buffer\n", bodySize);
        sendResourceGetError(clientId, requestId, ResourceError::ERROR);
        return;
    }

    // Read body into _auxBuf (sendResourceGetOk uses _txBuf for encoding)
    size_t bytesRead = prop->readResourceBody(resourceId, _auxBuf, bodySize);

    if (bytesRead == 0) {
        Serial.printf("[MicroProto] RESOURCE_GET: failed to read body for resource %lu\n", resourceId);
        sendResourceGetError(clientId, requestId, ResourceError::ERROR);
        return;
    }

    Serial.printf("[MicroProto] RESOURCE_GET: sending %d bytes for resource %lu\n", bytesRead, resourceId);
    sendResourceGetOk(clientId, requestId, _auxBuf, bytesRead);
}

void MicroProtoServer::onResourcePutRequest(uint8_t clientId, uint8_t requestId, uint16_t propertyId,
                                             uint32_t resourceId,
                                             const uint8_t* headerData, size_t headerLen,
                                             const uint8_t* bodyData, size_t bodyLen) {
    PropertyBase* prop = PropertyBase::find(static_cast<uint8_t>(propertyId));
    if (!prop || prop->getTypeId() != TYPE_RESOURCE) {
        Serial.printf("[MicroProto] RESOURCE_PUT: property %d not found or not RESOURCE type\n", propertyId);
        sendResourcePutError(clientId, requestId, ResourceError::NOT_FOUND);
        return;
    }

    if (resourceId == 0) {
        // Create new resource
        uint32_t newId = prop->createResource(headerData, headerLen, bodyData, bodyLen);
        if (newId == 0) {
            Serial.printf("[MicroProto] RESOURCE_PUT: failed to create resource\n");
            sendResourcePutError(clientId, requestId, ResourceError::OUT_OF_SPACE);
            return;
        }
        Serial.printf("[MicroProto] RESOURCE_PUT: created resource %lu\n", newId);
        sendResourcePutOk(clientId, requestId, newId);

        // Broadcast updated property to ALL clients (including requester - they need updated header list)
        broadcastPropertyExcept(prop, NO_EXCLUDE);
    } else {
        // Update existing resource
        bool success = true;
        if (headerData && headerLen > 0) {
            success = prop->updateResourceHeader(resourceId, headerData, headerLen);
        }
        if (success && bodyData && bodyLen > 0) {
            success = prop->updateResourceBody(resourceId, bodyData, bodyLen);
        }

        if (!success) {
            Serial.printf("[MicroProto] RESOURCE_PUT: failed to update resource %lu\n", resourceId);
            sendResourcePutError(clientId, requestId, ResourceError::NOT_FOUND);
            return;
        }

        Serial.printf("[MicroProto] RESOURCE_PUT: updated resource %lu\n", resourceId);
        sendResourcePutOk(clientId, requestId, resourceId);

        // Broadcast updated property to ALL clients (including requester - they need updated header list)
        broadcastPropertyExcept(prop, NO_EXCLUDE);
    }
}

void MicroProtoServer::onResourceDeleteRequest(uint8_t clientId, uint8_t requestId, uint16_t propertyId, uint32_t resourceId) {
    PropertyBase* prop = PropertyBase::find(static_cast<uint8_t>(propertyId));
    if (!prop || prop->getTypeId() != TYPE_RESOURCE) {
        Serial.printf("[MicroProto] RESOURCE_DELETE: property %d not found or not RESOURCE type\n", propertyId);
        sendResourceDeleteError(clientId, requestId, ResourceError::NOT_FOUND);
        return;
    }

    if (!prop->deleteResource(resourceId)) {
        Serial.printf("[MicroProto] RESOURCE_DELETE: failed to delete resource %lu\n", resourceId);
        sendResourceDeleteError(clientId, requestId, ResourceError::NOT_FOUND);
        return;
    }

    Serial.printf("[MicroProto] RESOURCE_DELETE: deleted resource %lu\n", resourceId);
    sendResourceDeleteOk(clientId, requestId);

    // Broadcast updated property to all clients
    broadcastPropertyExcept(prop, clientId);
}

// =========== Resource Response Helpers ===========

void MicroProtoServer::sendResourceGetOk(uint8_t clientNum, uint8_t requestId,
                                          const uint8_t* data, size_t len) {
    WriteBuffer wb(_txBuf, TX_BUFFER_SIZE);

    if (ResourceGetEncoder::encodeResponseOk(wb, requestId, data, len)) {
        _ws.sendBIN(clientNum, _txBuf, wb.position());
    }
}

void MicroProtoServer::sendResourceGetError(uint8_t clientNum, uint8_t requestId, uint8_t errorCode) {
    uint8_t buf[64];
    WriteBuffer wb(buf, sizeof(buf));

    if (ResourceGetEncoder::encodeResponseError(wb, requestId, errorCode)) {
        _ws.sendBIN(clientNum, buf, wb.position());
    }
}

void MicroProtoServer::sendResourcePutOk(uint8_t clientNum, uint8_t requestId, uint32_t resourceId) {
    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));

    if (ResourcePutEncoder::encodeResponseOk(wb, requestId, resourceId)) {
        _ws.sendBIN(clientNum, buf, wb.position());
    }
}

void MicroProtoServer::sendResourcePutError(uint8_t clientNum, uint8_t requestId, uint8_t errorCode) {
    uint8_t buf[64];
    WriteBuffer wb(buf, sizeof(buf));

    if (ResourcePutEncoder::encodeResponseError(wb, requestId, errorCode)) {
        _ws.sendBIN(clientNum, buf, wb.position());
    }
}

void MicroProtoServer::sendResourceDeleteOk(uint8_t clientNum, uint8_t requestId) {
    uint8_t buf[16];
    WriteBuffer wb(buf, sizeof(buf));

    if (ResourceDeleteEncoder::encodeResponseOk(wb, requestId)) {
        _ws.sendBIN(clientNum, buf, wb.position());
    }
}

void MicroProtoServer::sendResourceDeleteError(uint8_t clientNum, uint8_t requestId, uint8_t errorCode) {
    uint8_t buf[64];
    WriteBuffer wb(buf, sizeof(buf));

    if (ResourceDeleteEncoder::encodeResponseError(wb, requestId, errorCode)) {
        _ws.sendBIN(clientNum, buf, wb.position());
    }
}

} // namespace MicroProto
