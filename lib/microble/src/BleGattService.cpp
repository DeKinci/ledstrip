#include "BleGattService.h"

#ifndef NATIVE_TEST

#include "MicroBLE.h"
#include <MicroLog.h>

static const char* TAG = "GattSvc";

namespace MicroBLE {

void BleGattService::begin(GattHandler* handler, const GattConfig& config) {
    _handler = handler;
    _config = config;

    portMUX_INITIALIZE(&_mux);

    NimBLEServer* srv = MicroBLE::server();
    if (!srv) return;

    NimBLEService* service = srv->createService(_config.serviceUUID);

    uint32_t txProps = _config.txIndicate ? NIMBLE_PROPERTY::INDICATE : NIMBLE_PROPERTY::NOTIFY;
    _txChar = service->createCharacteristic(_config.txUUID, txProps);

    NimBLECharacteristic* rxChar = service->createCharacteristic(
        _config.rxUUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    rxChar->setCallbacks(this);

    service->start();

    // Register with MicroBLE for server-level callback dispatch
    MicroBLE::registerService(this);

    LOG_INFO(TAG, "Service started: %s", _config.serviceUUID);
}

// ---------------------------------------------------------------------------
// TX
// ---------------------------------------------------------------------------

bool BleGattService::send(uint8_t slot, const uint8_t* data, size_t len) {
    if (slot >= _config.maxClients) return false;

    uint16_t handle;
    portENTER_CRITICAL(&_mux);
    if (!_clients[slot].valid) {
        portEXIT_CRITICAL(&_mux);
        return false;
    }
    handle = _clients[slot].connHandle;
    portEXIT_CRITICAL(&_mux);

    _txChar->setValue(data, len);
    if (_config.txIndicate) {
        _txChar->indicate(handle);
    } else {
        _txChar->notify(handle);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Client info
// ---------------------------------------------------------------------------

uint16_t BleGattService::mtu(uint8_t slot) const {
    if (slot >= _config.maxClients) return 23;
    return _clients[slot].mtu;
}

bool BleGattService::isConnected(uint8_t slot) const {
    if (slot >= _config.maxClients) return false;
    return _clients[slot].valid;
}

uint8_t BleGattService::connectedCount() const {
    return _connCount;
}

uint16_t BleGattService::connHandle(uint8_t slot) const {
    if (slot >= _config.maxClients) return 0;
    return _clients[slot].connHandle;
}

// ---------------------------------------------------------------------------
// Server callback dispatch (called by MicroBLE's mux)
// ---------------------------------------------------------------------------

void BleGattService::onServerConnect(uint16_t connHandle) {
    portENTER_CRITICAL(&_mux);
    uint8_t slot = allocSlot(connHandle);
    portEXIT_CRITICAL(&_mux);

    if (slot < _config.maxClients && _handler) {
        _handler->onConnect(slot);
    }
}

void BleGattService::onServerDisconnect(uint16_t connHandle) {
    portENTER_CRITICAL(&_mux);
    uint8_t slot = findSlot(connHandle);
    freeSlot(connHandle);
    portEXIT_CRITICAL(&_mux);

    if (slot < _config.maxClients && _handler) {
        _handler->onDisconnect(slot);
    }
}

void BleGattService::onServerMTUChange(uint16_t connHandle, uint16_t mtu) {
    portENTER_CRITICAL(&_mux);
    uint8_t slot = findSlot(connHandle);
    if (slot < _config.maxClients) {
        _clients[slot].mtu = mtu;
    }
    portEXIT_CRITICAL(&_mux);

    if (slot < _config.maxClients && _handler) {
        _handler->onMTUChange(slot, mtu);
    }
}

// ---------------------------------------------------------------------------
// Characteristic write callback (per-characteristic, no mux needed)
// ---------------------------------------------------------------------------

void BleGattService::onWrite(NimBLECharacteristic* ch, NimBLEConnInfo& conn) {
    uint16_t handle = conn.getConnHandle();

    portENTER_CRITICAL(&_mux);
    uint8_t slot = findSlot(handle);
    portEXIT_CRITICAL(&_mux);

    if (slot >= _config.maxClients || !_handler) return;

    NimBLEAttValue val = ch->getValue();
    _handler->onWrite(slot, val.data(), val.size());
}

// ---------------------------------------------------------------------------
// Slot management (must be called under _mux)
// ---------------------------------------------------------------------------

uint8_t BleGattService::findSlot(uint16_t connHandle) const {
    for (uint8_t i = 0; i < _config.maxClients; i++) {
        if (_clients[i].valid && _clients[i].connHandle == connHandle) return i;
    }
    return _config.maxClients;
}

uint8_t BleGattService::allocSlot(uint16_t connHandle) {
    // Check for existing slot (reconnect)
    uint8_t existing = findSlot(connHandle);
    if (existing < _config.maxClients) return existing;

    for (uint8_t i = 0; i < _config.maxClients; i++) {
        if (!_clients[i].valid) {
            _clients[i].connHandle = connHandle;
            _clients[i].mtu = 23;
            _clients[i].valid = true;
            _connCount++;
            return i;
        }
    }
    return _config.maxClients;  // No slots available
}

void BleGattService::freeSlot(uint16_t connHandle) {
    for (uint8_t i = 0; i < _config.maxClients; i++) {
        if (_clients[i].valid && _clients[i].connHandle == connHandle) {
            _clients[i].valid = false;
            _connCount--;
            return;
        }
    }
}

} // namespace MicroBLE

#endif // NATIVE_TEST
