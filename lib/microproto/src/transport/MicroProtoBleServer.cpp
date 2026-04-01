#include "MicroProtoBleServer.h"
#include "../MicroProtoController.h"
#include <MicroLog.h>
#include <MicroBLE.h>

#define TAG "ProtoBLE"

#define MICROPROTO_SERVICE_UUID        "e3a10001-f5a3-4aa0-b726-5d1be14a1d00"
#define MICROPROTO_RX_CHAR_UUID        "e3a10002-f5a3-4aa0-b726-5d1be14a1d00"
#define MICROPROTO_TX_CHAR_UUID        "e3a10003-f5a3-4aa0-b726-5d1be14a1d00"

namespace MicroProto {

void MicroProtoBleServer::begin(MicroProtoController* controller) {
    _controller = controller;
    _clientIdOffset = controller->registerTransport(this);

    MicroBLE::GattConfig config = {};
    config.serviceUUID = MICROPROTO_SERVICE_UUID;
    config.rxUUID = MICROPROTO_RX_CHAR_UUID;
    config.txUUID = MICROPROTO_TX_CHAR_UUID;
    config.txIndicate = false;
    config.maxClients = MAX_CLIENTS;

    _gatt.begin(this, config);

    // Add service UUID to advertising and start
    MicroBLE::advertising()->addServiceUUID(MICROPROTO_SERVICE_UUID);
    MicroBLE::advertising()->enableScanResponse(true);
    MicroBLE::startAdvertising();

    LOG_INFO(TAG, "MicroProto BLE server started");
}

void MicroProtoBleServer::loop() {
    processRxQueue();
}

uint8_t MicroProtoBleServer::connectedClients() {
    return _gatt.connectedCount();
}

// =========== MicroProtoTransport ===========

void MicroProtoBleServer::send(uint8_t localClientId, const uint8_t* data, size_t len) {
    sendToClient(localClientId, data, len);
}

bool MicroProtoBleServer::isClientConnected(uint8_t localClientId) const {
    return _gatt.isConnected(localClientId);
}

uint32_t MicroProtoBleServer::maxPacketSize(uint8_t localClientId) const {
    return maxPayloadForClient(localClientId);
}

// =========== MicroBLE::GattHandler ===========

void MicroProtoBleServer::onConnect(uint8_t slot) {
    if (slot < MAX_CLIENTS) {
        _controller->onClientConnected(slot + _clientIdOffset);
        LOG_INFO(TAG, "BLE client connected: slot %u", slot);
    }

    // Restart advertising if room for more clients
    if (_gatt.connectedCount() < MAX_CLIENTS) {
        MicroBLE::startAdvertising();
    }
}

void MicroProtoBleServer::onDisconnect(uint8_t slot) {
    if (slot < MAX_CLIENTS) {
        _controller->onClientDisconnected(slot + _clientIdOffset);
        LOG_INFO(TAG, "BLE client disconnected: slot %u", slot);
    }
    MicroBLE::startAdvertising();
}

void MicroProtoBleServer::onMTUChange(uint8_t slot, uint16_t mtu) {
    LOG_INFO(TAG, "MTU changed to %u for slot %u", mtu, slot);
}

void MicroProtoBleServer::onWrite(uint8_t slot, const uint8_t* data, size_t len) {
    if (slot >= MAX_CLIENTS) return;

    auto& client = _clients[slot];

    // Backpressure: wait if main task hasn't consumed previous message
    if (client.messageReady.load(std::memory_order_acquire)) {
        for (uint8_t attempt = 0; attempt < RX_BACKPRESSURE_RETRIES; attempt++) {
            vTaskDelay(1);
            if (!client.messageReady.load(std::memory_order_acquire)) break;
        }
        if (client.messageReady.load(std::memory_order_acquire)) {
            LOG_WARN(TAG, "RX backpressure timeout, dropping fragment from client %u", slot);
            return;
        }
    }

    if (!client.reassembler.feed(data, len)) {
        return;  // Waiting for more fragments
    }

    client.messageReady.store(true, std::memory_order_release);

    // Queue signal for main task
    for (uint8_t attempt = 0; attempt <= RX_BACKPRESSURE_RETRIES; attempt++) {
        uint8_t head = _rxHead.load(std::memory_order_relaxed);
        uint8_t nextHead = (head + 1) % RX_QUEUE_SIZE;

        if (nextHead != _rxTail.load(std::memory_order_acquire)) {
            _rxQueue[head].clientId = slot;
            _rxHead.store(nextHead, std::memory_order_release);
            return;
        }

        if (attempt < RX_BACKPRESSURE_RETRIES) {
            vTaskDelay(1);
        }
    }

    LOG_WARN(TAG, "RX queue full, dropping message from client %u", slot);
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

uint16_t MicroProtoBleServer::maxPayloadForClient(uint8_t clientIdx) const {
    if (clientIdx >= MAX_CLIENTS) return 20;
    uint16_t m = _gatt.mtu(clientIdx);
    return m >= 23 ? m - 3 : 20;
}

void MicroProtoBleServer::sendToClient(uint8_t clientIdx, const uint8_t* data, size_t length) {
    if (clientIdx >= MAX_CLIENTS || !_gatt.isConnected(clientIdx)) return;

    uint16_t max = maxPayloadForClient(clientIdx);

    bleFragmentSend(data, length, max, [&](const uint8_t* frag, size_t fragLen) {
        _gatt.send(clientIdx, frag, fragLen);
    });
}

} // namespace MicroProto
