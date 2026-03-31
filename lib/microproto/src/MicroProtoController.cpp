#include "MicroProtoController.h"
#include "StreamProperty.h"
#include "messages/Schema.h"
#include "messages/Resource.h"
#include "wire/PropertyUpdate.h"
#include "wire/TypeCodec.h"

#ifdef ARDUINO
#include <Arduino.h>
#else
static unsigned long millis() { return 0; }
#endif
#include <MicroLog.h>

namespace MicroProto {

static const char* TAG = "MicroProto";

// =========== Lifecycle ===========

void MicroProtoController::begin() {
    PropertySystem::addFlushListener(this);
}

uint8_t MicroProtoController::registerTransport(MicroProtoTransport* transport) {
    if (_transportCount >= MAX_TRANSPORTS) return 0;

    uint8_t offset = 0;
    for (uint8_t i = 0; i < _transportCount; i++) {
        offset += _transports[i].clientCount;
    }

    auto& slot = _transports[_transportCount++];
    slot.transport = transport;
    slot.clientIdOffset = offset;
    slot.clientCount = transport->maxClients();

    LOG_INFO(TAG, "Transport registered: clients %d-%d (%d slots)",
             offset, offset + slot.clientCount - 1, slot.clientCount);
    return offset;
}

// =========== Routing ===========

MicroProtoController::TransportSlot* MicroProtoController::findTransport(uint8_t globalClientId) {
    for (uint8_t i = 0; i < _transportCount; i++) {
        auto& slot = _transports[i];
        if (globalClientId >= slot.clientIdOffset &&
            globalClientId < slot.clientIdOffset + slot.clientCount) {
            return &slot;
        }
    }
    return nullptr;
}

void MicroProtoController::sendToClient(uint8_t globalClientId, const uint8_t* data, size_t len) {
    auto* slot = findTransport(globalClientId);
    if (slot && slot->transport) {
        slot->transport->send(globalClientId - slot->clientIdOffset, data, len);
    }
}

bool MicroProtoController::requiresBleExposed(uint8_t globalClientId) {
    auto* slot = findTransport(globalClientId);
    return slot && slot->transport && slot->transport->capabilities().requiresBleExposed;
}

void MicroProtoController::processMessage(uint8_t globalClientId, const uint8_t* data, size_t len) {
    if (!_router.process(globalClientId, data, len)) {
        LOG_WARN(TAG, "Parse error from client %u", globalClientId);
        sendError(globalClientId, ErrorMessage(ErrorCode::INVALID_OPCODE, "Parse error"));
    }
}

void MicroProtoController::onClientConnected(uint8_t globalClientId) {
    if (globalClientId < MAX_TOTAL_CLIENTS) {
        _clientReady[globalClientId] = false;
    }
    LOG_INFO(TAG, "Client %u connected", globalClientId);
}

void MicroProtoController::onClientDisconnected(uint8_t globalClientId) {
    if (globalClientId < MAX_TOTAL_CLIENTS) {
        _clientReady[globalClientId] = false;
    }
    LOG_INFO(TAG, "Client %u disconnected", globalClientId);
}

uint8_t MicroProtoController::connectedClients() const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < _transportCount; i++) {
        auto& slot = _transports[i];
        for (uint8_t j = 0; j < slot.clientCount; j++) {
            if (slot.transport->isClientConnected(j)) count++;
        }
    }
    return count;
}

// =========== MessageHandler ===========

void MicroProtoController::onHello(uint8_t clientId, const Hello& hello) {
    if (hello.isResponse) {
        LOG_INFO(TAG, "HELLO response from client %u", clientId);
        return;
    }

    LOG_INFO(TAG, "HELLO from device 0x%08lX, version %d",
             static_cast<unsigned long>(hello.deviceId), hello.protocolVersion);

    if (hello.protocolVersion != PROTOCOL_VERSION) {
        sendError(clientId, ErrorMessage::protocolVersionMismatch());
        return;
    }

    if (clientId < MAX_TOTAL_CLIENTS) {
        _clientReady[clientId] = false;
    }

    // Idle mode: register but don't sync
    if (hello.idle) {
        Hello resp = Hello::idleResponse();
        uint8_t buf[32];
        WriteBuffer wb(buf, sizeof(buf));
        if (resp.encode(wb)) {
            sendToClient(clientId, buf, wb.position());
        }
        LOG_INFO(TAG, "Client %u registered idle", clientId);
        return;
    }

    sendHelloResponse(clientId);

    bool schemaMatch = hello.schemaVersion != 0
                    && hello.schemaVersion == PropertyBase::schemaVersion;
    if (schemaMatch) {
        LOG_INFO(TAG, "Client %u has schema v%u, skipping schema sync",
                 clientId, hello.schemaVersion);
    } else {
        sendSchema(clientId);
    }

    sendAllPropertyValues(clientId);

    if (clientId < MAX_TOTAL_CLIENTS) {
        _clientReady[clientId] = true;
    }
    LOG_INFO(TAG, "Client %u sync complete (schema %s)",
             clientId, schemaMatch ? "cached" : "sent");
}

void MicroProtoController::onPropertyUpdate(uint8_t clientId, uint16_t propertyId,
                                             const void* value, size_t size) {
    if (propertyId > 255) {
        LOG_WARN(TAG, "Property ID %d exceeds MVP limit", propertyId);
        return;
    }

    PropertyBase* p = PropertyBase::find(static_cast<uint8_t>(propertyId));
    if (!p) {
        LOG_WARN(TAG, "Unknown property ID: %d", propertyId);
        return;
    }

    if (requiresBleExposed(clientId) && !p->ble_exposed) {
        LOG_WARN(TAG, "Rejected write to non-BLE-exposed prop %d", propertyId);
        return;
    }

    if (p->readonly) {
        LOG_WARN(TAG, "Rejected write to readonly prop %d", propertyId);
        return;
    }

    p->setData(value, size);
    LOG_INFO(TAG, "Property %d updated by client %u", propertyId, clientId);
    broadcastPropertyExcept(p, clientId);
}

void MicroProtoController::onError(uint8_t clientId, const ErrorMessage& error) {
    LOG_WARN(TAG, "Error from client %u: code=%d",
             clientId, static_cast<uint16_t>(error.code));
}

void MicroProtoController::onPing(uint8_t clientId, bool isResponse, uint32_t payload) {
    if (isResponse) return;
    sendPong(clientId, payload);
}

void MicroProtoController::onRpcRequest(uint8_t clientId, uint16_t functionId, uint8_t callId,
                                         bool needsResponse, ReadBuffer& params) {
    FunctionBase* func = FunctionBase::find(static_cast<uint8_t>(functionId));
    if (!func) {
        LOG_WARN(TAG, "Unknown function ID %d from client %u", functionId, clientId);
        if (needsResponse) {
            sendRpcError(clientId, callId, 0x01, "Unknown function");
        }
        return;
    }

    if (requiresBleExposed(clientId) && !func->ble_exposed) {
        LOG_WARN(TAG, "Rejected RPC to non-BLE-exposed function %d", functionId);
        if (needsResponse) {
            sendRpcError(clientId, callId, 0x02, "Not available");
        }
        return;
    }

    WriteBuffer result(_txBuf, TX_BUFFER_SIZE);
    bool success = func->invoke(params, result);

    if (needsResponse) {
        if (success) {
            sendRpcResponse(clientId, callId, _txBuf, result.position());
        } else {
            sendRpcError(clientId, callId, 0x03, "Handler failed");
        }
    }

    LOG_INFO(TAG, "RPC %s (id=%d) from client %u: %s",
             func->name, functionId, clientId, success ? "ok" : "failed");
}

void MicroProtoController::onRpcResponse(uint8_t clientId, uint8_t callId,
                                          bool success, ReadBuffer& result) {
    LOG_INFO(TAG, "RPC response from client %u: callId=%u, success=%d",
             clientId, callId, success);
}

void MicroProtoController::onRpcError(uint8_t clientId, uint8_t callId,
                                       uint8_t errorCode, const char* message, size_t messageLen) {
    LOG_WARN(TAG, "RPC error from client %u: callId=%u, code=%u",
             clientId, callId, errorCode);
}

void MicroProtoController::onConstraintViolation(uint8_t clientId, uint16_t propertyId, ErrorCode code) {
    LOG_WARN(TAG, "Constraint violation on property %d from client %u", propertyId, clientId);
    sendError(clientId, ErrorMessage::validationFailed("Constraint violation"));
}

// =========== Resource Operations ===========

void MicroProtoController::onResourceGetRequest(uint8_t clientId, uint8_t requestId,
                                                 uint16_t propertyId, uint32_t resourceId) {
    PropertyBase* prop = PropertyBase::find(static_cast<uint8_t>(propertyId));
    if (!prop || prop->getTypeId() != TYPE_RESOURCE) {
        sendResourceGetError(clientId, requestId, 0x01);
        return;
    }

    if (requiresBleExposed(clientId) && !prop->ble_exposed) {
        sendResourceGetError(clientId, requestId, 0x02);
        return;
    }

    size_t bodySize = prop->getResourceBodySize(resourceId);
    if (bodySize == 0) {
        sendResourceGetError(clientId, requestId, 0x02);
        return;
    }

    size_t bytesRead = prop->readResourceBody(resourceId, _auxBuf, TX_BUFFER_SIZE);
    if (bytesRead == 0) {
        sendResourceGetError(clientId, requestId, 0x03);
        return;
    }

    sendResourceGetOk(clientId, requestId, _auxBuf, bytesRead);
}

void MicroProtoController::onResourcePutRequest(uint8_t clientId, uint8_t requestId,
                                                  uint16_t propertyId, uint32_t resourceId,
                                                  const uint8_t* headerData, size_t headerLen,
                                                  const uint8_t* bodyData, size_t bodyLen) {
    PropertyBase* prop = PropertyBase::find(static_cast<uint8_t>(propertyId));
    if (!prop || prop->getTypeId() != TYPE_RESOURCE) {
        sendResourcePutError(clientId, requestId, 0x01);
        return;
    }

    if (requiresBleExposed(clientId) && !prop->ble_exposed) {
        sendResourcePutError(clientId, requestId, 0x02);
        return;
    }

    uint32_t resultId;
    if (resourceId == 0) {
        resultId = prop->createResource(headerData, headerLen, bodyData, bodyLen);
        if (resultId == 0) {
            sendResourcePutError(clientId, requestId, 0x03);
            return;
        }
    } else {
        if (headerData && headerLen > 0) {
            prop->updateResourceHeader(resourceId, headerData, headerLen);
        }
        if (bodyData && bodyLen > 0) {
            prop->updateResourceBody(resourceId, bodyData, bodyLen);
        }
        resultId = resourceId;
    }

    sendResourcePutOk(clientId, requestId, resultId);
}

void MicroProtoController::onResourceDeleteRequest(uint8_t clientId, uint8_t requestId,
                                                     uint16_t propertyId, uint32_t resourceId) {
    PropertyBase* prop = PropertyBase::find(static_cast<uint8_t>(propertyId));
    if (!prop || prop->getTypeId() != TYPE_RESOURCE) {
        sendResourceDeleteError(clientId, requestId, 0x01);
        return;
    }

    if (requiresBleExposed(clientId) && !prop->ble_exposed) {
        sendResourceDeleteError(clientId, requestId, 0x02);
        return;
    }

    if (!prop->deleteResource(resourceId)) {
        sendResourceDeleteError(clientId, requestId, 0x03);
        return;
    }

    sendResourceDeleteOk(clientId, requestId);
}

// =========== FlushListener ===========

void MicroProtoController::onPropertiesChanged(const DirtySet& dirty) {
    for (uint8_t i = 0; i < PropertyBase::count; i++) {
        if (dirty.test(i)) {
            _pendingBroadcast.set(i);
        }
    }
}

// =========== Send Helpers ===========

void MicroProtoController::sendHelloResponse(uint8_t clientId) {
    // Get max packet size from the client's transport
    uint32_t maxPkt = TX_BUFFER_SIZE;
    auto* slot = findTransport(clientId);
    if (slot && slot->transport) {
        maxPkt = slot->transport->maxPacketSize(clientId - slot->clientIdOffset);
    }

    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));
    Hello response = Hello::response(_nextSessionId++, millis() / 1000, maxPkt, PropertyBase::schemaVersion);
    if (response.encode(wb)) {
        sendToClient(clientId, buf, wb.position());
    }
}

void MicroProtoController::sendSchema(uint8_t clientId) {
    bool bleFilter = requiresBleExposed(clientId);

    if (!bleFilter) {
        // Send all properties
        WriteBuffer wb(_txBuf, TX_BUFFER_SIZE);
        size_t count = SchemaEncoder::encodeAllProperties(wb);
        if (count > 0) {
            sendToClient(clientId, _txBuf, wb.position());
            LOG_INFO(TAG, "Sent schema (%d properties) to client %u", count, clientId);
        }

        // Send functions
        if (FunctionBase::count > 0) {
            WriteBuffer wbf(_txBuf, TX_BUFFER_SIZE);
            size_t fCount = SchemaEncoder::encodeAllFunctions(wbf);
            if (fCount > 0) {
                sendToClient(clientId, _txBuf, wbf.position());
                LOG_INFO(TAG, "Sent schema (%d functions) to client %u", fCount, clientId);
            }
        }
    } else {
        // BLE: filter by ble_exposed
        WriteBuffer wb(_txBuf, TX_BUFFER_SIZE);
        wb.writeByte(encodeOpHeader(OpCode::SCHEMA_UPSERT, Flags::BATCH));
        size_t countPos = wb.position();
        wb.writeByte(0);

        size_t sent = 0;
        for (uint8_t i = 0; i < PropertyBase::count; i++) {
            PropertyBase* prop = PropertyBase::byId[i];
            if (!prop || !prop->ble_exposed) continue;
            size_t rollback = wb.position();
            if (!SchemaEncoder::encodePropertyItem(wb, prop)) {
                wb.setPosition(rollback);
                continue;
            }
            sent++;
        }

        if (sent > 0) {
            _txBuf[countPos] = static_cast<uint8_t>(sent - 1);
            sendToClient(clientId, _txBuf, wb.position());
        }
        LOG_INFO(TAG, "Sent schema (%d/%d properties) to client %u", sent, PropertyBase::count, clientId);

        // Functions filtered
        WriteBuffer wbf(_txBuf, TX_BUFFER_SIZE);
        wbf.writeByte(encodeOpHeader(OpCode::SCHEMA_UPSERT, Flags::BATCH));
        size_t fCountPos = wbf.position();
        wbf.writeByte(0);

        size_t fSent = 0;
        for (uint8_t i = 0; i < FunctionBase::count; i++) {
            FunctionBase* func = FunctionBase::byId[i];
            if (!func || !func->ble_exposed) continue;
            size_t rollback = wbf.position();
            if (!SchemaEncoder::encodeFunctionItem(wbf, func)) {
                wbf.setPosition(rollback);
                continue;
            }
            fSent++;
        }
        if (fSent > 0) {
            _txBuf[fCountPos] = static_cast<uint8_t>(fSent - 1);
            sendToClient(clientId, _txBuf, wbf.position());
            LOG_INFO(TAG, "Sent schema (%d functions) to client %u", fSent, clientId);
        }
    }
}

void MicroProtoController::sendAllPropertyValues(uint8_t clientId) {
    bool bleFilter = requiresBleExposed(clientId);

    if (!bleFilter) {
        WriteBuffer wb(_txBuf, TX_BUFFER_SIZE);
        size_t count = PropertyEncoder::encodeAllValues(wb);
        if (count > 0) {
            sendToClient(clientId, _txBuf, wb.position());
            LOG_INFO(TAG, "Sent %d property values to client %u", count, clientId);
        }
    } else {
        WriteBuffer wb(_txBuf, TX_BUFFER_SIZE);
        PropertyUpdateFlags flags;
        flags.batch = true;
        wb.writeByte(encodeOpHeader(OpCode::PROPERTY_UPDATE, flags.encode()));
        size_t countPos = wb.position();
        wb.writeByte(0);

        size_t sent = 0;
        for (uint8_t i = 0; i < PropertyBase::count; i++) {
            PropertyBase* prop = PropertyBase::byId[i];
            if (!prop || !prop->ble_exposed) continue;
            size_t rollback = wb.position();
            if (!wb.writePropId(prop->id) || !TypeCodec::encodeProperty(wb, prop)) {
                wb.setPosition(rollback);
                continue;
            }
            sent++;
        }

        if (sent > 0) {
            _txBuf[countPos] = static_cast<uint8_t>(sent - 1);
            sendToClient(clientId, _txBuf, wb.position());
        }
        LOG_INFO(TAG, "Sent %d property values to client %u", sent, clientId);
    }
}

void MicroProtoController::sendError(uint8_t clientId, const ErrorMessage& error) {
    uint8_t buf[128];
    WriteBuffer wb(buf, sizeof(buf));
    if (error.encode(wb)) {
        sendToClient(clientId, buf, wb.position());
    }
}

void MicroProtoController::sendPong(uint8_t clientId, uint32_t payload) {
    uint8_t buf[16];
    WriteBuffer wb(buf, sizeof(buf));
    wb.writeByte(encodeOpHeader(OpCode::PING, Flags::IS_RESPONSE));
    wb.writeVarint(payload);
    sendToClient(clientId, buf, wb.position());
}

void MicroProtoController::sendRpcResponse(uint8_t clientId, uint8_t callId,
                                            const uint8_t* data, size_t len) {
    WriteBuffer wb(_txBuf, TX_BUFFER_SIZE);
    RpcFlags flags;
    flags.isResponse = true;
    flags.success = true;
    flags.hasReturnValue = (len > 0);
    wb.writeByte(encodeOpHeader(OpCode::RPC, flags.encode()));
    wb.writeByte(callId);
    if (len > 0) wb.writeBytes(data, len);
    sendToClient(clientId, _txBuf, wb.position());
}

void MicroProtoController::sendRpcError(uint8_t clientId, uint8_t callId,
                                         uint8_t errorCode, const char* message) {
    uint8_t buf[128];
    WriteBuffer wb(buf, sizeof(buf));
    RpcFlags flags;
    flags.isResponse = true;
    flags.success = false;
    wb.writeByte(encodeOpHeader(OpCode::RPC, flags.encode()));
    wb.writeByte(callId);
    wb.writeByte(errorCode);
    wb.writeUtf8(message);
    sendToClient(clientId, buf, wb.position());
}

void MicroProtoController::broadcastPropertyExcept(const PropertyBase* prop, uint8_t excludeClient) {
    WriteBuffer wb(_txBuf, TX_BUFFER_SIZE);
    if (!PropertyUpdate::encode(wb, prop)) return;

    for (uint8_t i = 0; i < MAX_TOTAL_CLIENTS; i++) {
        if (i != excludeClient && _clientReady[i]) {
            if (requiresBleExposed(i) && !prop->ble_exposed) continue;
            sendToClient(i, _txBuf, wb.position());
        }
    }
}

// =========== Resource Response Helpers ===========

void MicroProtoController::sendResourceGetOk(uint8_t clientId, uint8_t requestId,
                                               const uint8_t* data, size_t len) {
    WriteBuffer wb(_txBuf, TX_BUFFER_SIZE);
    wb.writeByte(encodeOpHeader(OpCode::RESOURCE_GET, Flags::IS_RESPONSE));
    wb.writeByte(requestId);
    wb.writeVarint(static_cast<uint32_t>(len));
    wb.writeBytes(data, len);
    if (wb.ok()) sendToClient(clientId, _txBuf, wb.position());
}

void MicroProtoController::sendResourceGetError(uint8_t clientId, uint8_t requestId, uint8_t errorCode) {
    uint8_t buf[8];
    WriteBuffer wb(buf, sizeof(buf));
    wb.writeByte(encodeOpHeader(OpCode::RESOURCE_GET, Flags::IS_RESPONSE | 0x02));
    wb.writeByte(requestId);
    wb.writeByte(errorCode);
    sendToClient(clientId, buf, wb.position());
}

void MicroProtoController::sendResourcePutOk(uint8_t clientId, uint8_t requestId, uint32_t resourceId) {
    uint8_t buf[16];
    WriteBuffer wb(buf, sizeof(buf));
    wb.writeByte(encodeOpHeader(OpCode::RESOURCE_PUT, Flags::IS_RESPONSE));
    wb.writeByte(requestId);
    wb.writeVarint(resourceId);
    sendToClient(clientId, buf, wb.position());
}

void MicroProtoController::sendResourcePutError(uint8_t clientId, uint8_t requestId, uint8_t errorCode) {
    uint8_t buf[8];
    WriteBuffer wb(buf, sizeof(buf));
    wb.writeByte(encodeOpHeader(OpCode::RESOURCE_PUT, Flags::IS_RESPONSE | 0x02));
    wb.writeByte(requestId);
    wb.writeByte(errorCode);
    sendToClient(clientId, buf, wb.position());
}

void MicroProtoController::sendResourceDeleteOk(uint8_t clientId, uint8_t requestId) {
    uint8_t buf[8];
    WriteBuffer wb(buf, sizeof(buf));
    wb.writeByte(encodeOpHeader(OpCode::RESOURCE_DELETE, Flags::IS_RESPONSE));
    wb.writeByte(requestId);
    sendToClient(clientId, buf, wb.position());
}

void MicroProtoController::sendResourceDeleteError(uint8_t clientId, uint8_t requestId, uint8_t errorCode) {
    uint8_t buf[8];
    WriteBuffer wb(buf, sizeof(buf));
    wb.writeByte(encodeOpHeader(OpCode::RESOURCE_DELETE, Flags::IS_RESPONSE | 0x02));
    wb.writeByte(requestId);
    wb.writeByte(errorCode);
    sendToClient(clientId, buf, wb.position());
}

// =========== Broadcast ===========

void MicroProtoController::flushBroadcasts() {
    if (!_pendingBroadcast.any()) return;

    // Check if any client is ready
    bool anyReady = false;
    for (uint8_t i = 0; i < MAX_TOTAL_CLIENTS; i++) {
        if (_clientReady[i]) { anyReady = true; break; }
    }
    if (!anyReady) {
        _pendingBroadcast.clearAll();
        return;
    }

    uint32_t now = millis();
    if (now - _lastBroadcastTime < BROADCAST_INTERVAL_MS) return;
    _lastBroadcastTime = now;

    // Collect dirty properties
    const PropertyBase* dirtyProps[PropertyBase::MAX_PROPERTIES];
    size_t dirtyCount = 0;

    for (uint8_t i = 0; i < PropertyBase::count; i++) {
        if (_pendingBroadcast.test(i)) {
            PropertyBase* p = PropertyBase::byId[i];
            if (p) dirtyProps[dirtyCount++] = p;
        }
    }

    if (dirtyCount == 0) {
        _pendingBroadcast.clearAll();
        return;
    }

    // Encode batch and send to each ready client
    // For BLE-exposed clients, filter properties
    size_t batchStart = 0;

    while (batchStart < dirtyCount) {
        WriteBuffer wb(_txBuf, TX_BUFFER_SIZE);
        PropertyUpdateFlags flags;
        flags.batch = true;
        wb.writeByte(encodeOpHeader(OpCode::PROPERTY_UPDATE, flags.encode()));
        size_t countPos = wb.position();
        wb.writeByte(0);

        size_t batchEnd = batchStart;
        while (batchEnd < dirtyCount) {
            size_t posBefore = wb.position();
            const PropertyBase* prop = dirtyProps[batchEnd];
            bool ok = wb.writePropId(prop->id);
            if (ok) {
                // Stream properties: encode only pending entries (delta)
                // Regular properties: encode full current value
                if (prop->isStream()) {
                    ok = prop->encodePendingEntries(wb);
                } else {
                    ok = TypeCodec::encodeProperty(wb, prop);
                }
            }
            if (!ok) {
                wb.setPosition(posBefore);
                break;
            }
            batchEnd++;
        }

        size_t batchCount = batchEnd - batchStart;
        if (batchCount == 0) break;

        _txBuf[countPos] = static_cast<uint8_t>(batchCount - 1);

        // Send to all ready, non-BLE clients
        for (uint8_t i = 0; i < MAX_TOTAL_CLIENTS; i++) {
            if (_clientReady[i] && !requiresBleExposed(i)) {
                sendToClient(i, _txBuf, wb.position());
            }
        }

        batchStart = batchEnd;
    }

    // BLE-filtered batch (separate encoding, only ble_exposed properties)
    bool anyBleClient = false;
    for (uint8_t i = 0; i < MAX_TOTAL_CLIENTS; i++) {
        if (_clientReady[i] && requiresBleExposed(i)) { anyBleClient = true; break; }
    }

    if (anyBleClient) {
        WriteBuffer wb(_txBuf, TX_BUFFER_SIZE);
        PropertyUpdateFlags flags;
        flags.batch = true;
        wb.writeByte(encodeOpHeader(OpCode::PROPERTY_UPDATE, flags.encode()));
        size_t countPos = wb.position();
        wb.writeByte(0);

        size_t sent = 0;
        for (size_t i = 0; i < dirtyCount; i++) {
            if (!dirtyProps[i]->ble_exposed) continue;
            size_t posBefore = wb.position();
            const PropertyBase* prop = dirtyProps[i];
            bool ok = wb.writePropId(prop->id);
            if (ok) {
                if (prop->isStream()) {
                    ok = prop->encodePendingEntries(wb);
                } else {
                    ok = TypeCodec::encodeProperty(wb, prop);
                }
            }
            if (!ok) {
                wb.setPosition(posBefore);
                break;
            }
            sent++;
        }

        if (sent > 0) {
            _txBuf[countPos] = static_cast<uint8_t>(sent - 1);
            for (uint8_t i = 0; i < MAX_TOTAL_CLIENTS; i++) {
                if (_clientReady[i] && requiresBleExposed(i)) {
                    sendToClient(i, _txBuf, wb.position());
                }
            }
        }
    }

    // Clear pending entries for stream properties after broadcast
    for (size_t i = 0; i < dirtyCount; i++) {
        if (dirtyProps[i]->isStream()) {
            // const_cast safe: clearPending is a non-const operation on our own properties
            const_cast<PropertyBase*>(dirtyProps[i])->clearPending();
        }
    }

    _pendingBroadcast.clearAll();
}

} // namespace MicroProto
