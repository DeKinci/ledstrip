#include "MicroProtoBleServer.h"
#include "../MicroProtoController.h"
#include <Arduino.h>
#include <Logger.h>

#define TAG "ProtoBLE"

#define MICROPROTO_SERVICE_UUID        "e3a10001-f5a3-4aa0-b726-5d1be14a1d00"
#define MICROPROTO_RX_CHAR_UUID        "e3a10002-f5a3-4aa0-b726-5d1be14a1d00"
#define MICROPROTO_TX_CHAR_UUID        "e3a10003-f5a3-4aa0-b726-5d1be14a1d00"

namespace MicroProto {

void MicroProtoBleServer::begin(MicroProtoController* controller) {
    _controller = controller;
    _clientIdOffset = controller->registerTransport(this);

    portMUX_INITIALIZE(&_clientsMux);
    _server = NimBLEDevice::createServer();
    _server->setCallbacks(this);

    NimBLEService* service = _server->createService(MICROPROTO_SERVICE_UUID);

    _txChar = service->createCharacteristic(
        MICROPROTO_TX_CHAR_UUID,
        NIMBLE_PROPERTY::NOTIFY
    );

    _rxChar = service->createCharacteristic(
        MICROPROTO_RX_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    _rxChar->setCallbacks(this);

    service->start();
    startAdvertising();
    LOG_INFO(TAG, "MicroProto BLE server started");
}

void MicroProtoBleServer::loop() {
    processRxQueue();
}

uint8_t MicroProtoBleServer::connectedClients() {
    return _connCount.load(std::memory_order_relaxed);
}

// =========== MicroProtoTransport ===========

void MicroProtoBleServer::send(uint8_t localClientId, const uint8_t* data, size_t len) {
    sendToClient(localClientId, data, len);
}

bool MicroProtoBleServer::isClientConnected(uint8_t localClientId) const {
    if (localClientId >= MAX_CLIENTS) return false;
    return _clients[localClientId].valid && _clients[localClientId].ready;
}

uint32_t MicroProtoBleServer::maxPacketSize(uint8_t localClientId) const {
    return maxPayloadForClient(localClientId);
}

// =========== NimBLE Callbacks ===========

void MicroProtoBleServer::onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) {
    uint16_t connHandle = connInfo.getConnHandle();

    portENTER_CRITICAL(&_clientsMux);
    uint8_t slot = allocClientSlot(connHandle);
    portEXIT_CRITICAL(&_clientsMux);

    if (slot < MAX_CLIENTS) {
        _controller->onClientConnected(slot + _clientIdOffset);
        LOG_INFO(TAG, "BLE client connected: slot %u, handle %u", slot, connHandle);
    }

    if (_server->getConnectedCount() < MAX_CLIENTS) {
        startAdvertising();
    }
}

void MicroProtoBleServer::onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) {
    uint16_t connHandle = connInfo.getConnHandle();

    portENTER_CRITICAL(&_clientsMux);
    uint8_t slot = findClientSlot(connHandle);
    freeClientSlot(connHandle);
    portEXIT_CRITICAL(&_clientsMux);

    if (slot < MAX_CLIENTS) {
        _controller->onClientDisconnected(slot + _clientIdOffset);
        LOG_INFO(TAG, "BLE client disconnected: slot %u, reason %d", slot, reason);
    }

    startAdvertising();
}

void MicroProtoBleServer::onMTUChange(uint16_t mtu, NimBLEConnInfo& connInfo) {
    uint16_t connHandle = connInfo.getConnHandle();
    portENTER_CRITICAL(&_clientsMux);
    uint8_t slot = findClientSlot(connHandle);
    if (slot < MAX_CLIENTS) {
        _clients[slot].mtu = mtu;
    }
    portEXIT_CRITICAL(&_clientsMux);
    LOG_INFO(TAG, "MTU changed to %u for slot %u", mtu, slot);
}

void MicroProtoBleServer::onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo) {
    uint16_t connHandle = connInfo.getConnHandle();

    portENTER_CRITICAL(&_clientsMux);
    uint8_t clientIdx = findClientSlot(connHandle);
    portEXIT_CRITICAL(&_clientsMux);

    if (clientIdx >= MAX_CLIENTS) return;

    auto& client = _clients[clientIdx];

    // Backpressure: wait if main task hasn't consumed previous message
    if (client.messageReady.load(std::memory_order_acquire)) {
        for (uint8_t attempt = 0; attempt < RX_BACKPRESSURE_RETRIES; attempt++) {
            vTaskDelay(1);
            if (!client.messageReady.load(std::memory_order_acquire)) break;
        }
        if (client.messageReady.load(std::memory_order_acquire)) {
            LOG_WARN(TAG, "RX backpressure timeout, dropping fragment from client %u", clientIdx);
            return;
        }
    }

    NimBLEAttValue val = characteristic->getValue();
    const uint8_t* data = val.data();
    size_t length = val.size();

    if (!client.reassembler.feed(data, length)) {
        return;  // Waiting for more fragments
    }

    client.messageReady.store(true, std::memory_order_release);

    // Queue signal for main task
    for (uint8_t attempt = 0; attempt <= RX_BACKPRESSURE_RETRIES; attempt++) {
        uint8_t head = _rxHead.load(std::memory_order_relaxed);
        uint8_t nextHead = (head + 1) % RX_QUEUE_SIZE;

        if (nextHead != _rxTail.load(std::memory_order_acquire)) {
            _rxQueue[head].clientId = clientIdx;
            _rxHead.store(nextHead, std::memory_order_release);
            return;
        }

        if (attempt < RX_BACKPRESSURE_RETRIES) {
            vTaskDelay(1);
        }
    }

    LOG_WARN(TAG, "RX queue full, dropping message from client %u", clientIdx);
    client.reassembler.reset();
    client.messageReady.store(false, std::memory_order_release);
}

// =========== Internal ===========

void MicroProtoBleServer::processRxQueue() {
    uint8_t tail = _rxTail.load(std::memory_order_relaxed);
    while (tail != _rxHead.load(std::memory_order_acquire)) {
        uint8_t clientId = _rxQueue[tail].clientId;
        auto& client = _clients[clientId];

        if (client.messageReady.load(std::memory_order_acquire)) {
            auto& reasm = client.reassembler;
            uint8_t globalId = clientId + _clientIdOffset;
            _controller->processMessage(globalId, reasm.buffer(), reasm.length());
            reasm.reset();
            client.messageReady.store(false, std::memory_order_release);
        }

        tail = (tail + 1) % RX_QUEUE_SIZE;
        _rxTail.store(tail, std::memory_order_release);
    }
}

uint8_t MicroProtoBleServer::findClientSlot(uint16_t connHandle) {
    for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
        if (_clients[i].valid && _clients[i].connHandle == connHandle) return i;
    }
    return MAX_CLIENTS;
}

uint8_t MicroProtoBleServer::allocClientSlot(uint16_t connHandle) {
    uint8_t existing = findClientSlot(connHandle);
    if (existing < MAX_CLIENTS) return existing;

    for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
        if (!_clients[i].valid) {
            _clients[i].connHandle = connHandle;
            _clients[i].mtu = 23;
            _clients[i].ready = false;
            _clients[i].reassembler.reset();
            _clients[i].messageReady.store(false, std::memory_order_relaxed);
            _clients[i].valid = true;
            _connCount.fetch_add(1, std::memory_order_relaxed);
            return i;
        }
    }
    return MAX_CLIENTS;
}

void MicroProtoBleServer::freeClientSlot(uint16_t connHandle) {
    for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
        if (_clients[i].valid && _clients[i].connHandle == connHandle) {
            _clients[i].valid = false;
            _clients[i].ready = false;
            _connCount.fetch_sub(1, std::memory_order_relaxed);
            if (!_clients[i].messageReady.load(std::memory_order_relaxed)) {
                _clients[i].reassembler.reset();
            }
            return;
        }
    }
}

uint16_t MicroProtoBleServer::maxPayloadForClient(uint8_t clientIdx) const {
    if (clientIdx >= MAX_CLIENTS) return 20;
    uint16_t mtu = _clients[clientIdx].mtu;
    return mtu >= 23 ? mtu - 3 : 20;
}

void MicroProtoBleServer::sendToClient(uint8_t clientIdx, const uint8_t* data, size_t length) {
    if (clientIdx >= MAX_CLIENTS) return;

    uint16_t max, connHandle;
    portENTER_CRITICAL(&_clientsMux);
    if (!_clients[clientIdx].valid) {
        portEXIT_CRITICAL(&_clientsMux);
        return;
    }
    max = maxPayloadForClient(clientIdx);
    connHandle = _clients[clientIdx].connHandle;
    portEXIT_CRITICAL(&_clientsMux);

    bleFragmentSend(data, length, max, [&](const uint8_t* frag, size_t fragLen) {
        _txChar->setValue(frag, fragLen);
        _txChar->notify(connHandle);
    });
}

void MicroProtoBleServer::startAdvertising() {
    if (!_advertisingConfigured) {
        NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
        advertising->addServiceUUID(MICROPROTO_SERVICE_UUID);
        advertising->enableScanResponse(true);
        _advertisingConfigured = true;
    }
    NimBLEDevice::startAdvertising();
    LOG_INFO(TAG, "BLE advertising started");
}

} // namespace MicroProto
