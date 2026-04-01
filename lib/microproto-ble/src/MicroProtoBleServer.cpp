#include "MicroProtoBleServer.h"
#include <MicroProtoController.h>
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

    _msg.begin(this, config);

    MicroBLE::advertising()->addServiceUUID(MICROPROTO_SERVICE_UUID);
    MicroBLE::advertising()->enableScanResponse(true);
    MicroBLE::startAdvertising();

    LOG_INFO(TAG, "MicroProto BLE server started");
}

void MicroProtoBleServer::loop() {
    _msg.loop();
}

uint8_t MicroProtoBleServer::connectedClients() {
    return _msg.connectedCount();
}

// --- MicroProtoTransport ---

void MicroProtoBleServer::send(uint8_t localClientId, const uint8_t* data, size_t len) {
    _msg.sendMessage(localClientId, data, len);
}

bool MicroProtoBleServer::isClientConnected(uint8_t localClientId) const {
    return _msg.isConnected(localClientId);
}

uint32_t MicroProtoBleServer::maxPacketSize(uint8_t localClientId) const {
    return _msg.payloadSize(localClientId);
}

// --- MicroBLE::MessageHandler ---

void MicroProtoBleServer::onMessage(uint8_t slot, const uint8_t* data, size_t len) {
    _controller->processMessage(slot + _clientIdOffset, data, len);
}

void MicroProtoBleServer::onConnect(uint8_t slot) {
    _controller->onClientConnected(slot + _clientIdOffset);
    LOG_INFO(TAG, "BLE client connected: slot %u", slot);

    if (_msg.connectedCount() < MAX_CLIENTS) {
        MicroBLE::startAdvertising();
    }
}

void MicroProtoBleServer::onDisconnect(uint8_t slot) {
    _controller->onClientDisconnected(slot + _clientIdOffset);
    LOG_INFO(TAG, "BLE client disconnected: slot %u", slot);
    MicroBLE::startAdvertising();
}

} // namespace MicroProto
