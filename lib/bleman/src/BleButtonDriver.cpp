#include "BleButtonDriver.h"
#include <MicroLog.h>

#define TAG "BleBtn"

// Static pool
std::array<BleButtonDriver, BLEMAN_MAX_CONNECTED_DEVICES> BleButtonDriver::_pool;

BleButtonDriver* BleButtonDriver::allocate() {
    for (auto& d : _pool) {
        if (!d._inUse) {
            d._inUse = true;
            return &d;
        }
    }
    LOG_WARN(TAG, "Button driver pool full");
    return nullptr;
}

int BleButtonDriver::poolIndex() const {
    for (size_t i = 0; i < _pool.size(); i++) {
        if (&_pool[i] == this) return i;
    }
    return -1;
}

bool BleButtonDriver::init(NimBLEClient* client) {
    _client = client;
    _pendingPress = false;
    _pendingRelease = false;
    _needsSubscribe = true;

    // Wire gesture -> sequence (per-instance via MicroFunction capture)
    _gesture.setCallback([this](BasicGesture g) {
        _sequence.onGesture(g);
    });

    // Wire sequence -> action callback
    _sequence.setCallback([this](SequenceDetector::Action a) {
        if (_onAction) _onAction(a);
    });

    return true;
}

void BleButtonDriver::loop() {
    if (_needsSubscribe && _client) {
        _needsSubscribe = false;
        subscribeToHID();
    }

    // Process deferred button events from BLE task
    if (_pendingPress) {
        _pendingPress = false;
        _gesture.onPress();
        _sequence.onPress();
    }
    if (_pendingRelease) {
        _pendingRelease = false;
        _gesture.onRelease();
        _sequence.onRelease();
    }

    _gesture.loop();
    _sequence.loop();
}

void BleButtonDriver::deinit() {
    _client = nullptr;
    _needsSubscribe = false;
    _pendingPress = false;
    _pendingRelease = false;
    _onAction = {};
    _serviceUUID = nullptr;
    _gesture = GestureDetector();
    _sequence = SequenceDetector();
    _inUse = false;
    LOG_INFO(TAG, "Driver released");
}

static const NimBLEUUID DEFAULT_HID_UUID(static_cast<uint16_t>(0x1812));

void BleButtonDriver::subscribeToHID() {
    NimBLEUUID uuid = _serviceUUID ? NimBLEUUID(_serviceUUID) : DEFAULT_HID_UUID;
    LOG_INFO(TAG, "Getting service %s...", uuid.toString().c_str());
    NimBLERemoteService* service = _client->getService(uuid);
    if (!service) {
        LOG_WARN(TAG, "HID service not found");
        return;
    }

    LOG_INFO(TAG, "Subscribing to HID characteristics...");
    const auto& chars = service->getCharacteristics(true);
    LOG_INFO(TAG, "Found %d characteristics", chars.size());

    int idx = poolIndex();
    if (idx < 0) return;

    // NimBLE notify callback must be a function pointer (no captures).
    // Dispatch by pool index to route events to the correct instance.
    using NotifyCB = void(*)(NimBLERemoteCharacteristic*, uint8_t*, size_t, bool);
    static const NotifyCB notifyCallbacks[] = {
        [](NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
            if (len == 0) return;
            bool pressed = false;
            for (size_t i = 0; i < len; i++) { if (data[i] != 0) pressed = true; }
            if (pressed) _pool[0]._pendingPress = true;
            else _pool[0]._pendingRelease = true;
        },
        [](NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
            if (len == 0) return;
            bool pressed = false;
            for (size_t i = 0; i < len; i++) { if (data[i] != 0) pressed = true; }
            if (pressed) _pool[1]._pendingPress = true;
            else _pool[1]._pendingRelease = true;
        },
        [](NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
            if (len == 0) return;
            bool pressed = false;
            for (size_t i = 0; i < len; i++) { if (data[i] != 0) pressed = true; }
            if (pressed) _pool[2]._pendingPress = true;
            else _pool[2]._pendingRelease = true;
        },
        [](NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
            if (len == 0) return;
            bool pressed = false;
            for (size_t i = 0; i < len; i++) { if (data[i] != 0) pressed = true; }
            if (pressed) _pool[3]._pendingPress = true;
            else _pool[3]._pendingRelease = true;
        },
    };
    static_assert(BLEMAN_MAX_CONNECTED_DEVICES <= 4,
                  "Add more notifyCallbacks entries if increasing max devices");

    for (auto* c : chars) {
        if (!c->canNotify()) continue;
        if (c->subscribe(true, notifyCallbacks[idx], false)) {
            LOG_INFO(TAG, "Subscribed to handle 0x%x", c->getHandle());
        } else {
            LOG_WARN(TAG, "Subscribe failed for handle 0x%x", c->getHandle());
        }
    }
}
