#ifndef NATIVE_TEST

#include "ble.h"
#include "config.h"
#include <Arduino.h>

class BLE::ServerCallbacks : public NimBLEServerCallbacks {
public:
    explicit ServerCallbacks(BLE& ble) : _ble(ble) {}

    void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override {
        _ble._connected = true;
        Serial.println("[BLE] Client connected");
    }

    void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override {
        _ble._connected = false;
        Serial.println("[BLE] Client disconnected");
        NimBLEDevice::startAdvertising();
    }

private:
    BLE& _ble;
};

class BLE::RxCallbacks : public NimBLECharacteristicCallbacks {
public:
    explicit RxCallbacks(BLE& ble) : _ble(ble) {}

    void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo) override {
        NimBLEAttValue value = characteristic->getValue();
        if (_ble._onReceive && value.length() > 0) {
            _ble._onReceive(value.data(), value.length());
        }
    }

private:
    BLE& _ble;
};

void BLE::begin(BleReceiveCallback onReceive) {
    _onReceive = onReceive;

    NimBLEDevice::init(BLE_DEVICE_NAME);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    NimBLEServer* server = NimBLEDevice::createServer();
    static ServerCallbacks serverCb(*this);
    server->setCallbacks(&serverCb);

    NimBLEService* service = server->createService(BLE_SERVICE_UUID);

    // TX characteristic: device → app (notify)
    _txChar = service->createCharacteristic(
        BLE_TX_CHARACTERISTIC,
        NIMBLE_PROPERTY::NOTIFY
    );

    // RX characteristic: app → device (write)
    NimBLECharacteristic* rxChar = service->createCharacteristic(
        BLE_RX_CHARACTERISTIC,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    static RxCallbacks rxCb(*this);
    rxChar->setCallbacks(&rxCb);

    service->start();

    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    advertising->addServiceUUID(BLE_SERVICE_UUID);
    advertising->enableScanResponse(true);
    advertising->start();

    Serial.println("[BLE] Advertising started");
}

bool BLE::isConnected() const {
    return _connected;
}

bool BLE::send(const uint8_t* data, size_t len) {
    if (!_connected || !_txChar || len == 0) return false;
    _txChar->setValue(data, len);
    _txChar->notify();
    return true;
}

#endif
