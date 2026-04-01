#include "MicroBLE.h"
#include "BleGattService.h"

#ifdef NATIVE_TEST

namespace MicroBLE {
static bool _initialized = false;
void init(const char*, int8_t) { _initialized = true; }
bool isInitialized() { return _initialized; }
void startAdvertising() {}
void stopAdvertising() {}
void registerService(BleGattService*) {}
} // namespace MicroBLE

#else

#include <MicroLog.h>

static const char* TAG = "MicroBLE";

namespace MicroBLE {

// ---------------------------------------------------------------------------
// Server callback multiplexer — dispatches to all registered BleGattServices
// ---------------------------------------------------------------------------

static constexpr uint8_t MAX_SERVICES = 4;

static BleGattService* _services[MAX_SERVICES] = {};
static uint8_t _serviceCount = 0;
static NimBLEServer* _server = nullptr;
static bool _initialized = false;

class ServerCallbackMux : public NimBLEServerCallbacks {
public:
    void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override {
        uint16_t handle = connInfo.getConnHandle();
        for (uint8_t i = 0; i < _serviceCount; i++) {
            _services[i]->onServerConnect(handle);
        }
    }

    void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override {
        uint16_t handle = connInfo.getConnHandle();
        for (uint8_t i = 0; i < _serviceCount; i++) {
            _services[i]->onServerDisconnect(handle);
        }
    }

    void onMTUChange(uint16_t mtu, NimBLEConnInfo& connInfo) override {
        uint16_t handle = connInfo.getConnHandle();
        for (uint8_t i = 0; i < _serviceCount; i++) {
            _services[i]->onServerMTUChange(handle, mtu);
        }
    }
};

static ServerCallbackMux _mux;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void init(const char* deviceName, int8_t power) {
    if (_initialized) return;

    NimBLEDevice::init(deviceName);
    NimBLEDevice::setPower((esp_power_level_t)power);
    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
    NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);

    _server = NimBLEDevice::createServer();
    _server->setCallbacks(&_mux);

    _initialized = true;
    LOG_INFO(TAG, "Initialized: %s", deviceName);
}

bool isInitialized() { return _initialized; }

NimBLEServer* server() { return _server; }

NimBLEAdvertising* advertising() { return NimBLEDevice::getAdvertising(); }

void startAdvertising() {
    NimBLEDevice::startAdvertising();
}

void stopAdvertising() {
    NimBLEDevice::stopAdvertising();
}

void registerService(BleGattService* svc) {
    if (_serviceCount < MAX_SERVICES) {
        _services[_serviceCount++] = svc;
    }
}

} // namespace MicroBLE

#endif // NATIVE_TEST
