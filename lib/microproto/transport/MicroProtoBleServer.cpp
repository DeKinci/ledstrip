#include "MicroProtoBleServer.h"
#include <Arduino.h>
#include <Logger.h>

#define TAG "ProtoBLE"

// Custom UUIDs for MicroProto BLE service
#define MICROPROTO_SERVICE_UUID        "e3a10001-f5a3-4aa0-b726-5d1be14a1d00"
#define MICROPROTO_RX_CHAR_UUID        "e3a10002-f5a3-4aa0-b726-5d1be14a1d00"
#define MICROPROTO_TX_CHAR_UUID        "e3a10003-f5a3-4aa0-b726-5d1be14a1d00"

namespace MicroProto {

void MicroProtoBleServer::begin() {
    _server = NimBLEDevice::createServer();
    _server->setCallbacks(this);

    NimBLEService* service = _server->createService(MICROPROTO_SERVICE_UUID);

    // TX: ESP32 -> BLE client (notify)
    _txChar = service->createCharacteristic(
        MICROPROTO_TX_CHAR_UUID,
        NIMBLE_PROPERTY::NOTIFY
    );

    // RX: BLE client -> ESP32 (write)
    _rxChar = service->createCharacteristic(
        MICROPROTO_RX_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    _rxChar->setCallbacks(this);

    service->start();

    PropertySystem::addFlushListener(this);

    startAdvertising();
    LOG_INFO(TAG, "MicroProto BLE server started");
}

void MicroProtoBleServer::loop() {
    processRxQueue();
    flushBroadcasts();
}

uint8_t MicroProtoBleServer::connectedClients() {
    uint8_t count = 0;
    for (const auto& c : _clients) {
        if (c.valid) count++;
    }
    return count;
}

// =========== NimBLE Server Callbacks ===========

void MicroProtoBleServer::onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) {
    uint16_t handle = connInfo.getConnHandle();
    uint8_t slot = allocClientSlot(handle);
    if (slot < MAX_CLIENTS) {
        LOG_INFO(TAG, "Client connected (handle=%u, slot=%u)", handle, slot);
    } else {
        LOG_WARN(TAG, "No slot for BLE client (handle=%u)", handle);
    }

    // Continue advertising if we have room for more clients
    if (connectedClients() < MAX_CLIENTS) {
        startAdvertising();
    }
}

void MicroProtoBleServer::onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) {
    uint16_t handle = connInfo.getConnHandle();
    LOG_INFO(TAG, "Client disconnected (handle=%u, reason=%d)", handle, reason);
    freeClientSlot(handle);
    startAdvertising();
}

void MicroProtoBleServer::onMTUChange(uint16_t mtu, NimBLEConnInfo& connInfo) {
    uint16_t handle = connInfo.getConnHandle();
    uint8_t slot = findClientSlot(handle);
    if (slot < MAX_CLIENTS) {
        _clients[slot].mtu = mtu;
        LOG_INFO(TAG, "MTU updated: %u (handle=%u)", mtu, handle);
    }
}

// =========== Characteristic Callbacks ===========

void MicroProtoBleServer::onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo) {
    // Runs on NimBLE task - queue the message for processing in loop()
    NimBLEAttValue val = characteristic->getValue();
    const uint8_t* data = val.data();
    size_t length = val.size();

    if (length == 0) return;

    uint16_t handle = connInfo.getConnHandle();
    uint8_t clientIdx = findClientSlot(handle);
    if (clientIdx >= MAX_CLIENTS) return;

    // Write to ring buffer (single producer from BLE task)
    uint8_t head = _rxHead.load(std::memory_order_relaxed);
    uint8_t nextHead = (head + 1) % RX_QUEUE_SIZE;
    if (nextHead == _rxTail.load(std::memory_order_acquire)) {
        LOG_WARN(TAG, "RX queue full, dropping message");
        return;
    }

    auto& slot = _rxQueue[head];
    size_t copyLen = length < TX_BUFFER_SIZE ? length : TX_BUFFER_SIZE;
    memcpy(slot.data, data, copyLen);
    slot.length = copyLen;
    slot.clientId = clientIdx;
    slot.valid = true;

    _rxHead.store(nextHead, std::memory_order_release);
}

// =========== MessageHandler ===========

void MicroProtoBleServer::onHello(uint8_t clientId, const Hello& hello) {
    if (hello.isResponse) {
        LOG_INFO(TAG, "Unexpected HELLO response from BLE client %u", clientId);
        return;
    }

    LOG_INFO(TAG, "HELLO from BLE device 0x%08lX, version %d",
             static_cast<unsigned long>(hello.deviceId), hello.protocolVersion);

    if (hello.protocolVersion != PROTOCOL_VERSION) {
        sendError(clientId, ErrorMessage::protocolVersionMismatch());
        return;
    }

    // Clear ready state before resync
    if (clientId < MAX_CLIENTS) {
        _clients[clientId].ready = false;
    }

    sendHelloResponse(clientId);
    sendSchema(clientId);
    sendAllPropertyValues(clientId);

    if (clientId < MAX_CLIENTS) {
        _clients[clientId].ready = true;
    }
    LOG_INFO(TAG, "BLE client %u sync complete", clientId);
}

void MicroProtoBleServer::onPropertyUpdate(uint8_t clientId, uint16_t propertyId, const void* value, size_t size) {
    if (propertyId > 255) {
        LOG_WARN(TAG, "Property ID %d exceeds MVP limit", propertyId);
        return;
    }

    PropertyBase* p = PropertyBase::find(static_cast<uint8_t>(propertyId));
    if (!p) {
        LOG_WARN(TAG, "Unknown property ID: %d", propertyId);
        return;
    }

    if (p->readonly) {
        LOG_WARN(TAG, "Rejected write to readonly prop %d", propertyId);
        return;
    }

    p->setData(value, size);
    LOG_INFO(TAG, "Property %d updated by BLE client %u", propertyId, clientId);
    broadcastPropertyExcept(p, clientId);
}

void MicroProtoBleServer::onError(uint8_t clientId, const ErrorMessage& error) {
    LOG_WARN(TAG, "Error from BLE client %u: code=%d", clientId, static_cast<uint16_t>(error.code));
}

void MicroProtoBleServer::onPing(uint8_t clientId, bool isResponse, uint32_t payload) {
    if (isResponse) return;
    sendPong(clientId, payload);
}

void MicroProtoBleServer::onConstraintViolation(uint8_t clientId, uint16_t propertyId, ErrorCode code) {
    LOG_WARN(TAG, "Constraint violation on property %d from BLE client %u", propertyId, clientId);
    sendError(clientId, ErrorMessage::validationFailed("Constraint violation"));
}

// =========== Client Slot Management ===========

uint8_t MicroProtoBleServer::findClientSlot(uint16_t connHandle) {
    for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
        if (_clients[i].valid && _clients[i].connHandle == connHandle) {
            return i;
        }
    }
    return MAX_CLIENTS;  // Not found
}

uint8_t MicroProtoBleServer::allocClientSlot(uint16_t connHandle) {
    // Check if already allocated
    uint8_t existing = findClientSlot(connHandle);
    if (existing < MAX_CLIENTS) return existing;

    // Find empty slot
    for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
        if (!_clients[i].valid) {
            _clients[i].connHandle = connHandle;
            _clients[i].mtu = 23;
            _clients[i].ready = false;
            _clients[i].valid = true;
            return i;
        }
    }
    return MAX_CLIENTS;  // No slot available
}

void MicroProtoBleServer::freeClientSlot(uint16_t connHandle) {
    for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
        if (_clients[i].valid && _clients[i].connHandle == connHandle) {
            _clients[i].valid = false;
            _clients[i].ready = false;
            return;
        }
    }
}

// =========== Send Helpers ===========

uint16_t MicroProtoBleServer::maxPayload(uint8_t clientIdx) const {
    if (clientIdx >= MAX_CLIENTS) return 20;
    uint16_t mtu = _clients[clientIdx].mtu;
    return mtu > 3 ? mtu - 3 : 20;
}

void MicroProtoBleServer::sendToClient(uint8_t clientIdx, const uint8_t* data, size_t length) {
    if (clientIdx >= MAX_CLIENTS || !_clients[clientIdx].valid) return;

    uint16_t max = maxPayload(clientIdx);
    if (length > max) {
        LOG_WARN(TAG, "Message %u bytes exceeds MTU payload %u, dropping", length, max);
        return;  // Drop instead of truncating - truncated binary messages are corrupt
    }

    _txChar->setValue(data, length);
    _txChar->notify(_clients[clientIdx].connHandle);
}

void MicroProtoBleServer::sendHelloResponse(uint8_t clientIdx) {
    uint16_t max = maxPayload(clientIdx);

    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));

    Hello response = Hello::response(_nextSessionId++, millis() / 1000, max);
    if (response.encode(wb)) {
        sendToClient(clientIdx, buf, wb.position());
    }
}

void MicroProtoBleServer::sendSchema(uint8_t clientIdx) {
    uint16_t max = maxPayload(clientIdx);

    // Send schema one property at a time to respect MTU.
    // Each SCHEMA_UPSERT is a self-contained message the client can parse.
    size_t count = PropertyBase::count;
    size_t sent = 0;

    for (uint8_t i = 0; i < count; i++) {
        PropertyBase* prop = PropertyBase::byId[i];
        if (!prop) continue;

        uint8_t buf[TX_BUFFER_SIZE];
        WriteBuffer wb(buf, sizeof(buf));

        if (SchemaEncoder::encodeProperty(wb, prop) && wb.position() <= max) {
            sendToClient(clientIdx, buf, wb.position());
            sent++;
        } else {
            LOG_WARN(TAG, "Schema for prop %d (%u bytes) exceeds MTU %u, skipping",
                     prop->id, wb.position(), max);
        }
    }

    LOG_INFO(TAG, "Sent schema (%d properties) to BLE client %u", sent, clientIdx);
}

void MicroProtoBleServer::sendAllPropertyValues(uint8_t clientIdx) {
    uint16_t max = maxPayload(clientIdx);

    // Send property values in MTU-respecting batches.
    // Batch header = 2 bytes (opcode + count). Try to pack multiple per message.
    size_t count = PropertyBase::count;
    size_t sent = 0;
    size_t propIdx = 0;

    while (propIdx < count) {
        uint8_t buf[TX_BUFFER_SIZE];
        size_t limit = max < TX_BUFFER_SIZE ? max : TX_BUFFER_SIZE;
        WriteBuffer wb(buf, limit);

        PropertyUpdateFlags flags;
        flags.batch = true;
        wb.writeByte(encodeOpHeader(OpCode::PROPERTY_UPDATE, flags.encode()));
        size_t countPos = wb.position();
        wb.writeByte(0);  // Placeholder for count-1

        size_t batchCount = 0;
        size_t lastGoodPos = 0;

        while (propIdx < count) {
            PropertyBase* prop = PropertyBase::byId[propIdx];
            if (!prop) { propIdx++; continue; }

            size_t posBefore = wb.position();
            if (!wb.writePropId(prop->id) || !TypeCodec::encodeProperty(wb, prop)) {
                wb.setPosition(posBefore);
                if (batchCount == 0) {
                    // Single property doesn't fit - skip it
                    LOG_WARN(TAG, "Property %d value exceeds MTU, skipping", prop->id);
                    propIdx++;
                }
                break;
            }

            lastGoodPos = wb.position();
            batchCount++;
            propIdx++;
        }

        if (batchCount > 0) {
            buf[countPos] = static_cast<uint8_t>(batchCount - 1);
            sendToClient(clientIdx, buf, lastGoodPos);
            sent += batchCount;
        }
    }

    LOG_INFO(TAG, "Sent %d property values to BLE client %u", sent, clientIdx);
}

void MicroProtoBleServer::sendError(uint8_t clientIdx, const ErrorMessage& error) {
    uint8_t buf[128];
    WriteBuffer wb(buf, sizeof(buf));

    if (error.encode(wb)) {
        sendToClient(clientIdx, buf, wb.position());
    }
}

void MicroProtoBleServer::sendPong(uint8_t clientIdx, uint32_t payload) {
    uint8_t buf[16];
    WriteBuffer wb(buf, sizeof(buf));

    wb.writeByte(encodeOpHeader(OpCode::PING, Flags::IS_RESPONSE));
    wb.writeVarint(payload);

    sendToClient(clientIdx, buf, wb.position());
}

void MicroProtoBleServer::broadcastPropertyExcept(const PropertyBase* prop, uint8_t excludeClient) {
    uint8_t buf[TX_BUFFER_SIZE];
    WriteBuffer wb(buf, sizeof(buf));

    if (!PropertyUpdate::encode(wb, prop)) return;

    for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
        if (i != excludeClient && _clients[i].valid && _clients[i].ready) {
            sendToClient(i, buf, wb.position());
        }
    }
}

// =========== Queue Processing ===========

void MicroProtoBleServer::processRxQueue() {
    uint8_t tail = _rxTail.load(std::memory_order_relaxed);
    while (tail != _rxHead.load(std::memory_order_acquire)) {
        auto& slot = _rxQueue[tail];
        if (slot.valid) {
            uint8_t clientId = slot.clientId;
            if (!_router.process(clientId, slot.data, slot.length)) {
                LOG_WARN(TAG, "Parse error from BLE client %u", clientId);
                sendError(clientId, ErrorMessage(ErrorCode::INVALID_OPCODE, "Parse error"));
            }
            slot.valid = false;
        }
        tail = (tail + 1) % RX_QUEUE_SIZE;
        _rxTail.store(tail, std::memory_order_release);
    }
}

// =========== Broadcasting ===========

void MicroProtoBleServer::onPropertiesChanged(const DirtySet& dirty) {
    for (uint8_t i = 0; i < PropertyBase::count; i++) {
        if (dirty.test(i)) {
            _pendingBroadcast.set(i);
        }
    }
}

void MicroProtoBleServer::flushBroadcasts() {
    if (!_pendingBroadcast.any()) return;

    if (connectedClients() == 0) {
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
            if (p) {
                dirtyProps[dirtyCount++] = p;
            }
        }
    }

    if (dirtyCount == 0) {
        _pendingBroadcast.clearAll();
        return;
    }

    // Find smallest MTU among ready clients to determine batch size limit.
    // This avoids encoding per-client (expensive) while still respecting MTU.
    uint16_t minPayload = TX_BUFFER_SIZE;
    for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
        if (_clients[i].valid && _clients[i].ready) {
            uint16_t mp = maxPayload(i);
            if (mp < minPayload) minPayload = mp;
        }
    }

    // Encode in MTU-respecting batches
    uint8_t buf[TX_BUFFER_SIZE];
    size_t batchStart = 0;

    while (batchStart < dirtyCount) {
        size_t limit = minPayload < TX_BUFFER_SIZE ? minPayload : TX_BUFFER_SIZE;
        WriteBuffer wb(buf, limit);

        PropertyUpdateFlags flags;
        flags.batch = true;
        wb.writeByte(encodeOpHeader(OpCode::PROPERTY_UPDATE, flags.encode()));
        size_t countPos = wb.position();
        wb.writeByte(0);  // Placeholder for count-1

        size_t batchEnd = batchStart;
        size_t lastGoodPos = 0;

        while (batchEnd < dirtyCount) {
            size_t posBefore = wb.position();
            const PropertyBase* prop = dirtyProps[batchEnd];
            if (!wb.writePropId(prop->id)) break;
            if (!TypeCodec::encodeProperty(wb, prop)) {
                wb.setPosition(posBefore);
                if (batchEnd == batchStart) {
                    // Single property doesn't fit - skip it
                    batchEnd++;
                }
                break;
            }
            lastGoodPos = wb.position();
            batchEnd++;
        }

        size_t batchCount = batchEnd - batchStart;
        if (batchCount == 0) break;

        buf[countPos] = static_cast<uint8_t>(batchCount - 1);

        // Send to all ready clients
        for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
            if (_clients[i].valid && _clients[i].ready) {
                sendToClient(i, buf, lastGoodPos);
            }
        }

        batchStart = batchEnd;
    }

    _pendingBroadcast.clearAll();
}

// =========== Advertising ===========

void MicroProtoBleServer::startAdvertising() {
    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    if (!_advertisingConfigured) {
        advertising->enableScanResponse(true);
        advertising->addServiceUUID(MICROPROTO_SERVICE_UUID);
        advertising->setName("SmartGarland");
        _advertisingConfigured = true;
    }
    advertising->start();
    LOG_DEBUG(TAG, "BLE advertising started");
}

} // namespace MicroProto
