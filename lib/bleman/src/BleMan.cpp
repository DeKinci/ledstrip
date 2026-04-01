#include "BleMan.h"
#include <Preferences.h>
#include <esp_coexist.h>
#include <MicroLog.h>

#define TAG "BleMan"

namespace BleMan {

// Static singleton for NimBLE callbacks
BleManager* BleManager::_instance = nullptr;

// Configuration
static constexpr uint32_t ACTIVE_SCAN_DURATION_MS = 10 * 1000;
static constexpr uint32_t CONNECTION_TIMEOUT_MS = 5000;

// HID service UUID (used in scan filtering)
static const NimBLEUUID HID_SERVICE_UUID(static_cast<uint16_t>(0x1812));

// --- Helpers ---

bool BleManager::addressEquals(const char* a, const char* b) {
    return strcasecmp(a, b) == 0;
}

template<typename T, size_t N>
int BleManager::findEmptySlot(const std::array<T, N>& arr) {
    for (size_t i = 0; i < N; i++) {
        if (!arr[i].valid) return i;
    }
    return -1;
}

template<typename T, size_t N>
size_t BleManager::countValid(const std::array<T, N>& arr) {
    size_t count = 0;
    for (const auto& item : arr) {
        if (item.valid) count++;
    }
    return count;
}

const char* BleManager::getIconNameFromAppearance(uint16_t appearance) {
    switch (appearance) {
        case 0x03C0: return "camera";
        case 0x03C1: return "media";
        case 0x03C2: return "gamepad";
        case 0x03C3: return "keyboard";
        case 0x0340: return "heart";
        case 0x0180: return "phone";
        case 0x0140: return "watch";
        case 0x0100: return "computer";
        case 0x00C0: return "headset";
        default: return "generic";
    }
}

// --- NimBLE Callback Classes ---
// These run on the BLE task. They only set state flags on the slot —
// all actual work (driver init, cleanup) happens in loop() on the main task.

class BleManager::DeviceClientCallbacks : public NimBLEClientCallbacks {
public:
    char address[18] = {0};
    bool inUse = false;

    void setAddress(const char* addr) {
        strncpy(address, addr, sizeof(address) - 1);
        address[sizeof(address) - 1] = '\0';
        inUse = true;
    }

    void clear() {
        inUse = false;
        address[0] = '\0';
    }

    void onConnect(NimBLEClient* client) override {
        LOG_INFO(TAG, "Connected to %s", address);
        auto* mgr = BleManager::_instance;
        if (!mgr) return;

        client->secureConnection(true);

        // Transition slot: CONNECTING → CONNECTED
        for (auto& dev : mgr->_connectedDevices) {
            if (dev.state == ConnState::CONNECTING && dev.client == client) {
                dev.state = ConnState::CONNECTED;
                break;
            }
        }
    }

    void onDisconnect(NimBLEClient* client, int reason) override {
        LOG_INFO(TAG, "Disconnected from %s (reason: %d)", address, reason);
        auto* mgr = BleManager::_instance;
        if (!mgr) return;

        // Transition slot → CLEANUP (from any active state)
        for (auto& dev : mgr->_connectedDevices) {
            if (dev.client == client && dev.state != ConnState::EMPTY) {
                dev.state = ConnState::CLEANUP;
                break;
            }
        }
    }
};

class BleManager::ActiveScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* device) override {
        auto* mgr = BleManager::_instance;
        if (!mgr) return;

        char addrStr[18];
        char nameStr[32];
        strncpy(addrStr, device->getAddress().toString().c_str(), sizeof(addrStr) - 1);
        addrStr[sizeof(addrStr) - 1] = '\0';
        if (device->getName().empty()) {
            strcpy(nameStr, "(unknown)");
        } else {
            strncpy(nameStr, device->getName().c_str(), sizeof(nameStr) - 1);
            nameStr[sizeof(nameStr) - 1] = '\0';
        }

        LOG_INFO(TAG, "Scan found: %s [%s] RSSI=%d", nameStr, addrStr, device->getRSSI());

        int existingIdx = -1;
        int emptyIdx = -1;
        for (size_t i = 0; i < BLEMAN_MAX_SCAN_RESULTS; i++) {
            if (mgr->_lastScanResults[i].valid && addressEquals(mgr->_lastScanResults[i].address, addrStr)) {
                existingIdx = i;
                break;
            }
            if (!mgr->_lastScanResults[i].valid && emptyIdx < 0) {
                emptyIdx = i;
            }
        }

        if (existingIdx >= 0) {
            mgr->_lastScanResults[existingIdx].lastSeen = millis();
            if (!device->getName().empty()) {
                strncpy(mgr->_lastScanResults[existingIdx].name, nameStr,
                        sizeof(mgr->_lastScanResults[existingIdx].name) - 1);
                mgr->_lastScanResults[existingIdx].name[sizeof(mgr->_lastScanResults[existingIdx].name) - 1] = '\0';
            }
        } else if (emptyIdx >= 0) {
            mgr->_lastScanResults[emptyIdx].set(
                addrStr,
                device->getName().empty() ? "" : nameStr,
                getIconNameFromAppearance(device->getAppearance()),
                "", false
            );
        }
    }

    void onScanEnd(const NimBLEScanResults& results, int reason) override {
        auto* mgr = BleManager::_instance;
        if (!mgr) return;
        LOG_INFO(TAG, "Active scan ended (%d devices, reason: %d)",
                 countValid(mgr->_lastScanResults), reason);
        mgr->_activeScanRunning = false;
        esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);
        mgr->startBackgroundScan();
    }
};

class BleManager::BackgroundScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* device) override {
        auto* mgr = BleManager::_instance;
        if (!mgr) return;

        char addrStr[18];
        strncpy(addrStr, device->getAddress().toString().c_str(), sizeof(addrStr) - 1);
        addrStr[sizeof(addrStr) - 1] = '\0';

        for (auto& known : mgr->_knownDevices) {
            if (!known.valid) continue;
            if (!addressEquals(known.address, addrStr)) continue;

            known.lastSeen = millis();

            if (!device->getName().empty() && known.name[0] == '\0') {
                strncpy(known.name, device->getName().c_str(), sizeof(known.name) - 1);
                known.name[sizeof(known.name) - 1] = '\0';
            }

            if (known.autoConnect) {
                bool alreadyActive = false;
                for (const auto& conn : mgr->_connectedDevices) {
                    if (conn.isActive() && addressEquals(conn.device.address, addrStr)) {
                        alreadyActive = true;
                        break;
                    }
                }

                bool alreadyQueued = false;
                for (size_t i = 0; i < mgr->_connectQueueCount; i++) {
                    if (mgr->_connectQueue[i] == device->getAddress()) {
                        alreadyQueued = true;
                        break;
                    }
                }

                if (!alreadyActive && !alreadyQueued &&
                    mgr->_connectQueueCount < BLEMAN_MAX_CONNECTED_DEVICES) {
                    LOG_INFO(TAG, "Queuing known device: %s", addrStr);
                    mgr->_connectQueue[mgr->_connectQueueCount++] = device->getAddress();
                }
            }
            break;
        }
    }

    void onScanEnd(const NimBLEScanResults& results, int reason) override {
        auto* mgr = BleManager::_instance;
        if (!mgr) return;
        LOG_INFO(TAG, "Background scan ended (reason: %d)", reason);
        mgr->_backgroundScanRunning = false;
    }
};

// Static callback instances — one per connection slot
static BleManager::DeviceClientCallbacks s_clientCallbacks[BLEMAN_MAX_CONNECTED_DEVICES];
static BleManager::ActiveScanCallbacks s_activeScanCallbacks;
static BleManager::BackgroundScanCallbacks s_backgroundScanCallbacks;

// --- BleManager Implementation ---

BleManager::BleManager(HttpDispatcher* dispatcher) : _dispatcher(dispatcher) {
    _instance = this;
}

void BleManager::begin() {
    esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);
    loadKnownDevices();
    updateWhitelist();
    setupRoutes();
    LOG_INFO(TAG, "Initialized (%d known, %d driver types)",
             getKnownDeviceCount(), getDriverTypeCount());
}

void BleManager::loop() {
    processConnectionQueue();
    processSlots();

    // Debug state
    static uint32_t lastDebug = 0;
    if (millis() - lastDebug > 5000) {
        lastDebug = millis();
        size_t active = 0;
        for (const auto& d : _connectedDevices) { if (d.isActive()) active++; }
        LOG_DEBUG(TAG, "bg=%d active=%d scan=%d known=%d slots=%d",
                  _backgroundScanRunning, active, _activeScanRunning,
                  countValid(_knownDevices), active);
    }

    // Keep background scan running when there are known devices and nothing is connecting
    bool anyConnecting = false;
    for (const auto& d : _connectedDevices) {
        if (d.state == ConnState::CONNECTING) { anyConnecting = true; break; }
    }
    if (!_backgroundScanRunning && !_activeScanRunning && !anyConnecting &&
        countValid(_knownDevices) > 0) {
        LOG_INFO(TAG, "Restarting background scan");
        startBackgroundScan();
    }

    // Run driver loops for connected devices
    for (auto& dev : _connectedDevices) {
        if (dev.state == ConnState::CONNECTED && dev.driver) {
            dev.driver->loop();
        }
    }
}

// --- Slot State Machine ---
// Single place that handles all slot transitions on the main task.

void BleManager::processSlots() {
    for (size_t i = 0; i < BLEMAN_MAX_CONNECTED_DEVICES; i++) {
        auto& dev = _connectedDevices[i];

        switch (dev.state) {
            case ConnState::EMPTY:
                break;

            case ConnState::CONNECTING:
                // Nothing to do — waiting for onConnect or onDisconnect callback
                // to transition us to CONNECTED or CLEANUP
                break;

            case ConnState::CONNECTED:
                // First time in CONNECTED state with no driver → init driver
                if (!dev.driver) {
                    initDriver(i);
                }
                // Check for unexpected disconnect (NimBLE set connHandle to NONE)
                if (!dev.client->isConnected()) {
                    LOG_INFO(TAG, "Detected disconnect for %s", dev.device.address);
                    dev.state = ConnState::CLEANUP;
                }
                break;

            case ConnState::DISCONNECTING:
                // Waiting for onDisconnect callback → CLEANUP.
                // Also handle case where disconnect already happened.
                if (!dev.client->isConnected()) {
                    dev.state = ConnState::CLEANUP;
                }
                break;

            case ConnState::CLEANUP:
                cleanupSlot(i);
                break;
        }
    }
}

void BleManager::initDriver(size_t i) {
    auto& dev = _connectedDevices[i];
    BleDriverFactory factory = findDriverFactory(dev.device.type);
    if (factory) {
        BleDriver* driver = factory(dev.device);
        if (driver && driver->init(dev.client)) {
            dev.driver = driver;
            LOG_INFO(TAG, "Driver '%s' initialized for %s",
                     dev.device.type, dev.device.name);
        } else {
            LOG_WARN(TAG, "Driver init failed for %s", dev.device.name);
            if (driver) driver->deinit();
        }
    } else if (dev.device.type[0] != '\0') {
        LOG_WARN(TAG, "No driver registered for type '%s'", dev.device.type);
    }
    // Set driver to a sentinel so we don't retry init every loop.
    // nullptr means "no driver" which is valid (unknown type).
    // We use the driver pointer itself: non-null = initialized, null = no driver needed.
    // To distinguish "no factory" from "not yet initialized", we mark the slot.
    // Since we only call initDriver once (when driver == nullptr && state == CONNECTED),
    // this is fine — after this call, either driver is set or it stays null (no driver).
}

void BleManager::cleanupSlot(size_t i) {
    auto& dev = _connectedDevices[i];
    LOG_INFO(TAG, "Cleaning up slot %d (%s)", i, dev.device.address);

    if (dev.driver) {
        dev.driver->deinit();
        dev.driver = nullptr;
    }

    // Release NimBLE callback
    for (auto& cb : s_clientCallbacks) {
        if (cb.inUse && addressEquals(cb.address, dev.device.address)) {
            cb.clear();
            break;
        }
    }

    if (dev.client) {
        NimBLEDevice::deleteClient(dev.client);
    }

    dev.client = nullptr;
    dev.driver = nullptr;
    dev.state = ConnState::EMPTY;
}

// --- Driver System ---

void BleManager::registerDriver(const char* type, BleDriverFactory factory) {
    for (auto& reg : _driverTypes) {
        if (reg.valid && strcasecmp(reg.type, type) == 0) {
            reg.factory = factory;
            return;
        }
    }

    for (auto& reg : _driverTypes) {
        if (!reg.valid) {
            strncpy(reg.type, type, sizeof(reg.type) - 1);
            reg.type[sizeof(reg.type) - 1] = '\0';
            reg.factory = factory;
            reg.valid = true;
            LOG_INFO(TAG, "Registered driver: %s", type);
            return;
        }
    }
    LOG_WARN(TAG, "Driver type table full, cannot register: %s", type);
}

const std::array<DriverRegistration, BLEMAN_MAX_DRIVER_TYPES>& BleManager::getDriverTypes() const {
    return _driverTypes;
}

size_t BleManager::getDriverTypeCount() const {
    return countValid(_driverTypes);
}

BleDriverFactory BleManager::findDriverFactory(const char* type) const {
    for (const auto& reg : _driverTypes) {
        if (reg.valid && strcasecmp(reg.type, type) == 0) {
            return reg.factory;
        }
    }
    return nullptr;
}

// --- Device Management ---

bool BleManager::addKnownDevice(const char* address, const char* name,
                                const char* icon, const char* type,
                                bool autoConnect) {
    for (auto& dev : _knownDevices) {
        if (dev.valid && addressEquals(dev.address, address)) {
            dev.set(address, name, icon, type, autoConnect);
            saveKnownDevices();
            updateWhitelist();
            return true;
        }
    }

    int slot = findEmptySlot(_knownDevices);
    if (slot < 0) {
        LOG_WARN(TAG, "No room for new known device");
        return false;
    }

    _knownDevices[slot].set(address, name, icon, type, autoConnect);
    saveKnownDevices();
    updateWhitelist();

    if (!_activeScanRunning) {
        stopBackgroundScan();
        startBackgroundScan();
    }

    LOG_INFO(TAG, "Added known device: %s (%s) type=%s", name, address, type);
    return true;
}

bool BleManager::removeKnownDevice(const char* address) {
    for (auto& dev : _knownDevices) {
        if (dev.valid && addressEquals(dev.address, address)) {
            stopBackgroundScan();

            // Remove from connection queue
            for (size_t i = 0; i < _connectQueueCount; ) {
                char qAddr[18];
                strncpy(qAddr, _connectQueue[i].toString().c_str(), sizeof(qAddr) - 1);
                qAddr[sizeof(qAddr) - 1] = '\0';
                if (addressEquals(qAddr, address)) {
                    _connectQueue[i] = _connectQueue[--_connectQueueCount];
                } else {
                    i++;
                }
            }

            // Transition any active slot for this address → CLEANUP
            for (auto& conn : _connectedDevices) {
                if (conn.isActive() && addressEquals(conn.device.address, address)) {
                    if (conn.state == ConnState::CONNECTED || conn.state == ConnState::DISCONNECTING) {
                        conn.client->disconnect();
                    }
                    conn.state = ConnState::CLEANUP;
                }
            }

            dev.clear();
            saveKnownDevices();
            updateWhitelist();
            LOG_INFO(TAG, "Removed known device: %s", address);
            return true;
        }
    }
    return false;
}

const std::array<BleKnownDevice, BLEMAN_MAX_KNOWN_DEVICES>& BleManager::getKnownDevices() const {
    return _knownDevices;
}

size_t BleManager::getKnownDeviceCount() const {
    return countValid(_knownDevices);
}

bool BleManager::isKnownDevice(const char* address) const {
    for (const auto& dev : _knownDevices) {
        if (dev.valid && addressEquals(dev.address, address)) return true;
    }
    return false;
}

// --- Connection Management ---

bool BleManager::connectToDevice(const char* address) {
    for (const auto& conn : _connectedDevices) {
        if (conn.isActive() && addressEquals(conn.device.address, address)) {
            LOG_INFO(TAG, "Already active for %s", address);
            return true;
        }
    }

    for (const auto& known : _knownDevices) {
        if (known.valid && addressEquals(known.address, address)) {
            if (_connectQueueCount >= BLEMAN_MAX_CONNECTED_DEVICES) {
                LOG_WARN(TAG, "Connection queue full");
                return false;
            }
            _connectQueue[_connectQueueCount++] = NimBLEAddress(std::string(address), BLE_ADDR_PUBLIC);
            return true;
        }
    }

    LOG_WARN(TAG, "Cannot connect to unknown device: %s", address);
    return false;
}

bool BleManager::disconnectDevice(const char* address) {
    for (auto& dev : _connectedDevices) {
        if (!dev.isActive() || !addressEquals(dev.device.address, address)) continue;

        switch (dev.state) {
            case ConnState::CONNECTED:
                dev.client->disconnect();
                dev.state = ConnState::DISCONNECTING;
                return true;
            case ConnState::CONNECTING:
            case ConnState::DISCONNECTING:
                // Already transitioning — force to cleanup
                dev.state = ConnState::CLEANUP;
                return true;
            case ConnState::CLEANUP:
                return true;  // Already cleaning up
            default:
                break;
        }
    }
    return false;
}

BleConnectedDevice* BleManager::getConnectedDevice(const char* address) {
    for (auto& conn : _connectedDevices) {
        if (conn.isActive() && addressEquals(conn.device.address, address)) return &conn;
    }
    return nullptr;
}

const std::array<BleConnectedDevice, BLEMAN_MAX_CONNECTED_DEVICES>& BleManager::getConnectedDevices() const {
    return _connectedDevices;
}

size_t BleManager::getConnectedDeviceCount() const {
    size_t count = 0;
    for (const auto& d : _connectedDevices) {
        if (d.isConnected()) count++;
    }
    return count;
}

// --- Scanning ---

void BleManager::triggerScanNow() {
    if (_activeScanRunning) {
        LOG_INFO(TAG, "Active scan already running");
        return;
    }
    stopBackgroundScan();
    startActiveScan();
}

bool BleManager::isScanning() const {
    return _activeScanRunning;
}

const std::array<BleKnownDevice, BLEMAN_MAX_SCAN_RESULTS>& BleManager::getLastScanResults() const {
    return _lastScanResults;
}

size_t BleManager::getLastScanResultCount() const {
    return countValid(_lastScanResults);
}

// --- Storage ---

static String nvsKey(int index, const char* field) {
    char buf[16];
    snprintf(buf, sizeof(buf), "dev%d_%s", index, field);
    return String(buf);
}

void BleManager::saveKnownDevices() {
    Preferences prefs;
    if (!prefs.begin("ble-devices", false)) {
        LOG_ERROR(TAG, "Failed to open preferences");
        return;
    }

    prefs.clear();

    size_t count = 0;
    for (size_t i = 0; i < BLEMAN_MAX_KNOWN_DEVICES; i++) {
        if (!_knownDevices[i].valid) continue;

        prefs.putString(nvsKey(count, "addr").c_str(), _knownDevices[i].address);
        prefs.putString(nvsKey(count, "name").c_str(), _knownDevices[i].name);
        prefs.putString(nvsKey(count, "icon").c_str(), _knownDevices[i].icon);
        prefs.putString(nvsKey(count, "type").c_str(), _knownDevices[i].type);
        prefs.putBool(nvsKey(count, "auto").c_str(), _knownDevices[i].autoConnect);
        count++;
    }

    prefs.putUInt("count", count);
    prefs.end();
    LOG_INFO(TAG, "Saved %d known devices", count);
}

void BleManager::loadKnownDevices() {
    Preferences prefs;
    if (!prefs.begin("ble-devices", true)) {
        LOG_INFO(TAG, "No saved devices found");
        return;
    }

    for (auto& dev : _knownDevices) dev.clear();

    uint32_t count = prefs.getUInt("count", 0);
    size_t loaded = 0;

    for (uint32_t i = 0; i < count && loaded < BLEMAN_MAX_KNOWN_DEVICES; i++) {
        String addr = prefs.getString(nvsKey(i, "addr").c_str(), "");

        if (!addr.isEmpty()) {
            _knownDevices[loaded].set(
                addr.c_str(),
                prefs.getString(nvsKey(i, "name").c_str(), "").c_str(),
                prefs.getString(nvsKey(i, "icon").c_str(), "generic").c_str(),
                prefs.getString(nvsKey(i, "type").c_str(), "").c_str(),
                prefs.getBool(nvsKey(i, "auto").c_str(), true)
            );
            _knownDevices[loaded].lastSeen = 0;
            loaded++;
        }
    }

    prefs.end();
    LOG_INFO(TAG, "Loaded %d known devices", loaded);
}

// --- Scanning Implementation ---

void BleManager::startActiveScan() {
    for (auto& result : _lastScanResults) result.clear();

    esp_coex_preference_set(ESP_COEX_PREFER_BT);

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&s_activeScanCallbacks);
    scan->setInterval(100);
    scan->setWindow(99);
    scan->setActiveScan(true);
    scan->setDuplicateFilter(true);
    scan->setFilterPolicy(BLE_HCI_SCAN_FILT_NO_WL);
    scan->clearResults();

    if (scan->start(ACTIVE_SCAN_DURATION_MS, false, false)) {
        _activeScanRunning = true;
        LOG_INFO(TAG, "Active scan started (%lu ms)", ACTIVE_SCAN_DURATION_MS);
    } else {
        LOG_ERROR(TAG, "Failed to start active scan");
        startBackgroundScan();
    }
}

void BleManager::stopActiveScan() {
    if (_activeScanRunning) {
        NimBLEDevice::getScan()->stop();
        _activeScanRunning = false;
        esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);
        LOG_INFO(TAG, "Active scan stopped");
    }
}

void BleManager::startBackgroundScan() {
    if (_backgroundScanRunning || _activeScanRunning) return;
    if (countValid(_knownDevices) == 0) {
        LOG_INFO(TAG, "No known devices, skipping background scan");
        return;
    }

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&s_backgroundScanCallbacks);
    scan->setInterval(160);
    scan->setWindow(48);
    scan->setActiveScan(false);
    scan->setDuplicateFilter(false);
    scan->setFilterPolicy(BLE_HCI_SCAN_FILT_USE_WL);
    scan->clearResults();

    if (scan->start(0, false, false)) {
        _backgroundScanRunning = true;
        LOG_INFO(TAG, "Background scan started (whitelist: %d)", NimBLEDevice::getWhiteListCount());
    } else {
        LOG_ERROR(TAG, "Failed to start background scan");
    }
}

void BleManager::stopBackgroundScan() {
    if (_backgroundScanRunning) {
        NimBLEDevice::getScan()->stop();
        _backgroundScanRunning = false;
        LOG_INFO(TAG, "Background scan stopped");
    }
}

void BleManager::updateWhitelist() {
    int count = static_cast<int>(NimBLEDevice::getWhiteListCount());
    for (int i = 0; i < count && i < 32; i++) {
        NimBLEAddress addr = NimBLEDevice::getWhiteListAddress(0);
        if (!NimBLEDevice::whiteListRemove(addr)) {
            LOG_WARN(TAG, "Failed to remove from whitelist: %s", addr.toString().c_str());
            break;
        }
    }

    for (const auto& dev : _knownDevices) {
        if (!dev.valid || !dev.autoConnect) continue;

        NimBLEAddress addr(std::string(dev.address), BLE_ADDR_PUBLIC);
        if (NimBLEDevice::whiteListAdd(addr)) {
            LOG_INFO(TAG, "Whitelist: %s", dev.address);
        } else {
            NimBLEAddress addrRandom(std::string(dev.address), BLE_ADDR_RANDOM);
            if (NimBLEDevice::whiteListAdd(addrRandom)) {
                LOG_INFO(TAG, "Whitelist (random): %s", dev.address);
            }
        }
    }

    LOG_INFO(TAG, "Whitelist updated: %d entries", NimBLEDevice::getWhiteListCount());
}

// --- Connection Queue ---

void BleManager::processConnectionQueue() {
    if (_connectQueueCount == 0) return;

    // Stop active scan if we have a pending connection — don't wait 10s
    if (_activeScanRunning) {
        LOG_INFO(TAG, "Stopping active scan for pending connection");
        stopActiveScan();
    }

    // Don't start a new connection while one is in progress
    for (const auto& d : _connectedDevices) {
        if (d.state == ConnState::CONNECTING) return;
    }

    NimBLEAddress addr = _connectQueue[--_connectQueueCount];
    char addrStr[18];
    strncpy(addrStr, addr.toString().c_str(), sizeof(addrStr) - 1);
    addrStr[sizeof(addrStr) - 1] = '\0';

    LOG_INFO(TAG, "Connecting to %s", addrStr);

    BleKnownDevice* known = nullptr;
    for (auto& k : _knownDevices) {
        if (k.valid && addressEquals(k.address, addrStr)) {
            known = &k;
            break;
        }
    }
    if (!known) return;

    // Find empty slot
    int slot = -1;
    for (size_t i = 0; i < BLEMAN_MAX_CONNECTED_DEVICES; i++) {
        if (_connectedDevices[i].state == ConnState::EMPTY) { slot = i; break; }
    }
    if (slot < 0) return;

    NimBLEClient* client = NimBLEDevice::getDisconnectedClient();
    if (!client) client = NimBLEDevice::createClient();
    if (!client) return;

    // Use the same-index callback object (1:1 with slot)
    s_clientCallbacks[slot].setAddress(addrStr);
    client->setClientCallbacks(&s_clientCallbacks[slot], false);

    client->setConnectionParams(12, 12, 0, 150);
    client->setConnectTimeout(CONNECTION_TIMEOUT_MS);

    _connectedDevices[slot].client = client;
    _connectedDevices[slot].device = *known;
    _connectedDevices[slot].driver = nullptr;
    _connectedDevices[slot].state = ConnState::CONNECTING;

    _backgroundScanRunning = false;

    LOG_INFO(TAG, "Calling connect() for %s...", addrStr);
    bool connectResult = client->connect(addr, false, true, true);

    if (!connectResult && addr.getType() == BLE_ADDR_PUBLIC) {
        LOG_INFO(TAG, "PUBLIC failed, trying RANDOM for %s", addrStr);
        NimBLEAddress addrRandom(std::string(addrStr), BLE_ADDR_RANDOM);
        connectResult = client->connect(addrRandom, false, true, true);
    }

    if (!connectResult) {
        LOG_WARN(TAG, "Failed to connect to %s", addrStr);
        _connectedDevices[slot].state = ConnState::CLEANUP;
    } else {
        LOG_INFO(TAG, "connect() returned true");
    }
}

} // namespace BleMan
