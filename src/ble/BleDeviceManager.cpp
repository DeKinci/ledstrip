#include "BleDeviceManager.hpp"
#include "BleButton.hpp"
#include <Preferences.h>
#include <esp_coexist.h>
#include <Logger.h>

#define TAG "BLE"

namespace {

// Configuration
constexpr uint32_t ACTIVE_SCAN_DURATION_MS = 10 * 1000;  // 10 seconds in milliseconds (NimBLE 2.x uses ms)
constexpr uint32_t CONNECTION_TIMEOUT_MS = 5000;
constexpr uint16_t INVALID_CONN_HANDLE = 65535;  // NimBLE uses this for invalid/unset connection handles

// storage (no heap allocation)
std::array<KnownDevice, BLE_MAX_KNOWN_DEVICES> knownDevices;
std::array<ConnectedDevice, BLE_MAX_CONNECTED_DEVICES> connectedDevices;
std::array<KnownDevice, BLE_MAX_SCAN_RESULTS> lastScanResults;
std::array<BleButton, BLE_MAX_CONNECTED_DEVICES> buttonPool;

// Connection queues (fixed size)
std::array<NimBLEAddress, BLE_MAX_CONNECTED_DEVICES> devicesToConnect;
size_t devicesToConnectCount = 0;

std::array<NimBLEClient*, BLE_MAX_CONNECTED_DEVICES> clientsNeedingInit;
size_t clientsNeedingInitCount = 0;

std::array<NimBLEClient*, BLE_MAX_CONNECTED_DEVICES> clientsDisconnected;
size_t clientsDisconnectedCount = 0;

// State flags
bool activeScanRunning = false;
bool backgroundScanRunning = false;
bool connectionInProgress = false;

// Callbacks (function pointers)
DeviceConnectedCallback onDeviceConnected = nullptr;
DeviceDisconnectedCallback onDeviceDisconnected = nullptr;
DeviceDiscoveredCallback onDeviceDiscovered = nullptr;

// Helper: case-insensitive address compare
bool addressEquals(const char* a, const char* b) {
    return strcasecmp(a, b) == 0;
}

// Helper: find empty slot in array
template<typename T, size_t N>
int findEmptySlot(const std::array<T, N>& arr) {
    for (size_t i = 0; i < N; i++) {
        if (!arr[i].valid) return i;
    }
    return -1;
}

// Helper: count valid items
template<typename T, size_t N>
size_t countValid(const std::array<T, N>& arr) {
    size_t count = 0;
    for (const auto& item : arr) {
        if (item.valid) count++;
    }
    return count;
}

// Helper to get icon from appearance
const char* getIconNameFromAppearance(uint16_t appearance) {
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

// Client callbacks - one per connected device slot (static pool)
class DeviceClientCallbacks : public NimBLEClientCallbacks {
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
        LOG_INFO(TAG, "Connected to %s (callback on BLE task)", address);
        connectionInProgress = false;  // Connection complete
        client->secureConnection(true);

        // Queue client for BleButton initialization in main loop
        if (clientsNeedingInitCount < BLE_MAX_CONNECTED_DEVICES) {
            clientsNeedingInit[clientsNeedingInitCount++] = client;
            LOG_DEBUG(TAG, "Queued client %p for init", (void*)client);
        } else {
            LOG_WARN(TAG, "clientsNeedingInit queue full!");
        }
    }

    void onDisconnect(NimBLEClient* client, int reason) override {
        LOG_INFO(TAG, "Disconnected from %s (reason: %d)", address, reason);
        connectionInProgress = false;  // Clear in case disconnect during connection establishment

        if (onDeviceDisconnected) {
            onDeviceDisconnected(address, reason);
        }

        // Queue for cleanup in main loop
        if (clientsDisconnectedCount < BLE_MAX_CONNECTED_DEVICES) {
            clientsDisconnected[clientsDisconnectedCount++] = client;
        } else {
            LOG_WARN(TAG, "clientsDisconnected queue full!");
        }
    }
};

std::array<DeviceClientCallbacks, BLE_MAX_CONNECTED_DEVICES> callbackPool;

// Find available callback slot
DeviceClientCallbacks* getAvailableCallback() {
    for (auto& cb : callbackPool) {
        if (!cb.inUse) return &cb;
    }
    return nullptr;
}

// HID service UUID
const NimBLEUUID HID_SERVICE_UUID(static_cast<uint16_t>(0x1812));

// Active scan callbacks
class ActiveScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* device) override {
        // Copy strings to local buffers (toString()/getName() return temporaries)
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
        bool isHID = device->isAdvertisingService(HID_SERVICE_UUID);

        LOG_INFO(TAG, "Active found: %s [%s] HID=%d RSSI=%d", nameStr, addrStr, isHID, device->getRSSI());

        // Find existing or empty slot in scan results
        int existingIdx = -1;
        int emptyIdx = -1;
        for (size_t i = 0; i < BLE_MAX_SCAN_RESULTS; i++) {
            if (lastScanResults[i].valid && addressEquals(lastScanResults[i].address, addrStr)) {
                existingIdx = i;
                break;
            }
            if (!lastScanResults[i].valid && emptyIdx < 0) {
                emptyIdx = i;
            }
        }

        if (existingIdx >= 0) {
            lastScanResults[existingIdx].lastSeen = millis();
            if (!device->getName().empty()) {
                strncpy(lastScanResults[existingIdx].name, nameStr,
                       sizeof(lastScanResults[existingIdx].name) - 1);
                lastScanResults[existingIdx].name[sizeof(lastScanResults[existingIdx].name) - 1] = '\0';
            }
        } else if (emptyIdx >= 0) {
            lastScanResults[emptyIdx].set(
                addrStr,
                device->getName().empty() ? "" : nameStr,
                getIconNameFromAppearance(device->getAppearance()),
                false  // autoConnect off for scan results
            );
        }

        if (onDeviceDiscovered) {
            onDeviceDiscovered(device);
        }
    }

    void onScanEnd(const NimBLEScanResults& results, int reason) override {
        LOG_INFO(TAG, "Active scan ended (found %d devices, reason: %d)", countValid(lastScanResults), reason);
        activeScanRunning = false;
        esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);
        BleDeviceManager::startBackgroundScan();
    }
};

// Background scan callbacks
class BackgroundScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* device) override {
        // Copy strings to local buffers (toString() returns temporary)
        char addrStr[18];
        strncpy(addrStr, device->getAddress().toString().c_str(), sizeof(addrStr) - 1);
        addrStr[sizeof(addrStr) - 1] = '\0';

        // Find the known device
        for (auto& known : knownDevices) {
            if (!known.valid) continue;
            if (!addressEquals(known.address, addrStr)) continue;

            known.lastSeen = millis();

            // Update name if we have a better one
            if (!device->getName().empty() && known.name[0] == '\0') {
                strncpy(known.name, device->getName().c_str(), sizeof(known.name) - 1);
                known.name[sizeof(known.name) - 1] = '\0';
            }

            // Check if we should auto-connect
            if (known.autoConnect) {
                bool alreadyConnected = false;
                bool alreadyQueued = false;

                for (const auto& conn : connectedDevices) {
                    if (conn.valid && addressEquals(conn.device.address, addrStr)) {
                        alreadyConnected = true;
                        break;
                    }
                }

                for (size_t i = 0; i < devicesToConnectCount; i++) {
                    if (devicesToConnect[i] == device->getAddress()) {
                        alreadyQueued = true;
                        break;
                    }
                }

                if (!alreadyConnected && !alreadyQueued && devicesToConnectCount < BLE_MAX_CONNECTED_DEVICES) {
                    LOG_INFO(TAG, "Queuing known device for connection: %s", addrStr);
                    devicesToConnect[devicesToConnectCount++] = device->getAddress();
                }
            }
            break;
        }
    }

    void onScanEnd(const NimBLEScanResults& results, int reason) override {
        LOG_INFO(TAG, "Background scan ended (reason: %d)", reason);
        backgroundScanRunning = false;
    }
};

ActiveScanCallbacks activeScanCallbacks;
BackgroundScanCallbacks backgroundScanCallbacks;

}  // namespace

void BleDeviceManager::init() {
    esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);

    NimBLEDevice::init("SmartGarland");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
    NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);

    loadKnownDevices();
    updateWhitelist();
    // Don't start scan in init - let loop() start it after a brief delay
    // This avoids duplicate filter issues when BLE device is already advertising on boot
    LOG_INFO(TAG, "Initialized (scan will start in loop)");
}

void BleDeviceManager::loop() {
    // Process connection queue
    if (devicesToConnectCount > 0 && !activeScanRunning) {
        NimBLEAddress addr = devicesToConnect[--devicesToConnectCount];
        // Copy to local buffer (toString() returns temporary)
        char addrStr[18];
        strncpy(addrStr, addr.toString().c_str(), sizeof(addrStr) - 1);
        addrStr[sizeof(addrStr) - 1] = '\0';

        LOG_INFO(TAG, "Attempting to connect to %s", addrStr);

        // Find the known device
        KnownDevice* known = nullptr;
        for (auto& k : knownDevices) {
            if (k.valid && addressEquals(k.address, addrStr)) {
                known = &k;
                break;
            }
        }

        if (known) {
            // Find empty slot for connection
            int slot = findEmptySlot(connectedDevices);
            if (slot >= 0) {
                NimBLEClient* client = NimBLEDevice::getDisconnectedClient();
                if (!client) {
                    client = NimBLEDevice::createClient();
                }

                if (client) {
                    // Get callback from pool
                    DeviceClientCallbacks* cb = getAvailableCallback();
                    if (cb) {
                        cb->setAddress(addrStr);
                        client->setClientCallbacks(cb, false);  // false = we manage lifetime
                    }

                    client->setConnectionParams(12, 12, 0, 150);
                    client->setConnectTimeout(CONNECTION_TIMEOUT_MS);

                    // Reserve slot before connecting
                    connectedDevices[slot].set(client, *known);

                    backgroundScanRunning = false;
                    connectionInProgress = true;  // Set before async connect

                    LOG_INFO(TAG, "Calling connect() for %s...", addrStr);
                    bool connectResult = client->connect(addr, false, true, true);

                    // If PUBLIC address fails, try RANDOM
                    if (!connectResult && addr.getType() == BLE_ADDR_PUBLIC) {
                        LOG_INFO(TAG, "PUBLIC failed, trying RANDOM for %s", addrStr);
                        NimBLEAddress addrRandom(std::string(addrStr), BLE_ADDR_RANDOM);
                        connectResult = client->connect(addrRandom, false, true, true);
                    }

                    if (!connectResult) {
                        LOG_WARN(TAG, "Failed to connect to %s", addrStr);
                        connectionInProgress = false;  // Clear on failure
                        connectedDevices[slot].clear();
                        if (cb) cb->clear();
                        NimBLEDevice::deleteClient(client);
                    } else {
                        LOG_INFO(TAG, "connect() returned true, waiting for callback");
                    }
                }
            }
        }
    }

    // Process deferred BleButton initialization
    while (clientsNeedingInitCount > 0) {
        NimBLEClient* client = clientsNeedingInit[--clientsNeedingInitCount];

        LOG_DEBUG(TAG, "Processing deferred init for client %p", (void*)client);

        for (size_t i = 0; i < BLE_MAX_CONNECTED_DEVICES; i++) {
            auto& dev = connectedDevices[i];
            if (dev.valid && dev.client == client && dev.userData == nullptr) {
                // Use button from pool (same index as connection slot)
                buttonPool[i] = BleButton(client);
                dev.userData = &buttonPool[i];
                LOG_INFO(TAG, "Initialized BleButton for %s", dev.device.name);

                if (onDeviceConnected) {
                    onDeviceConnected(dev);
                }
                break;
            }
        }
    }

    // Process deferred disconnect cleanup
    while (clientsDisconnectedCount > 0) {
        NimBLEClient* client = clientsDisconnected[--clientsDisconnectedCount];

        LOG_DEBUG(TAG, "Processing deferred disconnect for client %p", (void*)client);

        for (size_t i = 0; i < BLE_MAX_CONNECTED_DEVICES; i++) {
            auto& dev = connectedDevices[i];
            if (dev.valid && dev.client == client) {
                // Clear callback in pool (must do before clearing dev)
                for (auto& cb : callbackPool) {
                    if (cb.inUse && addressEquals(cb.address, dev.device.address)) {
                        cb.clear();
                        break;
                    }
                }

                dev.userData = nullptr;  // Button is in pool, just clear pointer
                dev.clear();
                LOG_INFO(TAG, "Removed disconnected device");
                break;
            }
        }
    }

    // Debug state every 5 seconds
    static uint32_t lastScanCheck = 0;
    if (millis() - lastScanCheck > 5000) {
        lastScanCheck = millis();
        LOG_DEBUG(TAG, "STATE bgScan=%d, activeScan=%d, connecting=%d, known=%d, connected=%d",
                  backgroundScanRunning, activeScanRunning, connectionInProgress,
                  countValid(knownDevices), countValid(connectedDevices));
    }

    // Keep background scan running (but not during connection establishment)
    if (!backgroundScanRunning && !activeScanRunning && !connectionInProgress && countValid(knownDevices) > 0) {
        LOG_INFO(TAG, "Restarting background scan");
        startBackgroundScan();
    }

    // Call loop on all connected button devices
    for (auto& device : connectedDevices) {
        if (device.valid && device.userData != nullptr) {
            static_cast<BleButton*>(device.userData)->loop();
        }
    }

    cleanupDisconnectedClients();
}

bool BleDeviceManager::addKnownDevice(const char* address, const char* name,
                                     const char* icon, bool autoConnect) {
    // Check if already exists
    for (auto& dev : knownDevices) {
        if (dev.valid && addressEquals(dev.address, address)) {
            dev.set(address, name, icon, autoConnect);
            saveKnownDevices();
            updateWhitelist();
            return true;
        }
    }

    // Find empty slot
    int slot = findEmptySlot(knownDevices);
    if (slot < 0) {
        LOG_WARN(TAG, "No room for new known device");
        return false;
    }

    knownDevices[slot].set(address, name, icon, autoConnect);
    saveKnownDevices();
    updateWhitelist();

    if (!activeScanRunning) {
        stopBackgroundScan();
        startBackgroundScan();
    }

    LOG_INFO(TAG, "Added known device: %s (%s)", name, address);
    return true;
}

bool BleDeviceManager::removeKnownDevice(const char* address) {
    for (auto& dev : knownDevices) {
        if (dev.valid && addressEquals(dev.address, address)) {
            stopBackgroundScan();  // Must stop scan before modifying whitelist
            disconnectDevice(address);
            dev.clear();
            saveKnownDevices();
            updateWhitelist();
            LOG_INFO(TAG, "Removed known device: %s", address);
            // loop() will restart background scan if there are still known devices
            return true;
        }
    }
    return false;
}

const std::array<KnownDevice, BLE_MAX_KNOWN_DEVICES>& BleDeviceManager::getKnownDevices() {
    return knownDevices;
}

size_t BleDeviceManager::getKnownDeviceCount() {
    return countValid(knownDevices);
}

bool BleDeviceManager::isKnownDevice(const char* address) {
    for (const auto& dev : knownDevices) {
        if (dev.valid && addressEquals(dev.address, address)) {
            return true;
        }
    }
    return false;
}

bool BleDeviceManager::connectToDevice(const char* address) {
    // Check if already connected
    for (const auto& conn : connectedDevices) {
        if (conn.valid && addressEquals(conn.device.address, address)) {
            LOG_INFO(TAG, "Already connected to %s", address);
            return true;
        }
    }

    // Add to connection queue if known
    for (const auto& known : knownDevices) {
        if (known.valid && addressEquals(known.address, address)) {
            if (devicesToConnectCount >= BLE_MAX_CONNECTED_DEVICES) {
                LOG_WARN(TAG, "Connection queue full, cannot connect to %s", address);
                return false;
            }
            // Try PUBLIC first - NimBLE will handle address type detection during connect
            // For devices with RANDOM addresses, the whitelist already has both types
            devicesToConnect[devicesToConnectCount++] = NimBLEAddress(std::string(address), BLE_ADDR_PUBLIC);
            return true;
        }
    }

    LOG_WARN(TAG, "Cannot connect to unknown device: %s", address);
    return false;
}

bool BleDeviceManager::disconnectDevice(const char* address) {
    for (size_t i = 0; i < BLE_MAX_CONNECTED_DEVICES; i++) {
        auto& dev = connectedDevices[i];
        if (dev.valid && addressEquals(dev.device.address, address)) {
            // Just trigger disconnect - let the deferred cleanup in loop() handle the rest
            // This avoids race conditions with async disconnect callback
            dev.client->disconnect();
            // Note: Don't clear callback or delete client here - onDisconnect callback
            // will fire async and queue the client for cleanup in loop()
            return true;
        }
    }
    return false;
}

ConnectedDevice* BleDeviceManager::getConnectedDevice(const char* address) {
    for (auto& conn : connectedDevices) {
        if (conn.valid && addressEquals(conn.device.address, address)) {
            return &conn;
        }
    }
    return nullptr;
}

const std::array<ConnectedDevice, BLE_MAX_CONNECTED_DEVICES>& BleDeviceManager::getConnectedDevices() {
    return connectedDevices;
}

size_t BleDeviceManager::getConnectedDeviceCount() {
    return countValid(connectedDevices);
}

void BleDeviceManager::triggerScanNow() {
    if (activeScanRunning) {
        LOG_INFO(TAG, "Active scan already running");
        return;
    }
    stopBackgroundScan();
    startActiveScan();
}

bool BleDeviceManager::isScanning() {
    return activeScanRunning;
}

const std::array<KnownDevice, BLE_MAX_SCAN_RESULTS>& BleDeviceManager::getLastScanResults() {
    return lastScanResults;
}

size_t BleDeviceManager::getLastScanResultCount() {
    return countValid(lastScanResults);
}

void BleDeviceManager::setOnDeviceConnected(DeviceConnectedCallback callback) {
    onDeviceConnected = callback;
}

void BleDeviceManager::setOnDeviceDisconnected(DeviceDisconnectedCallback callback) {
    onDeviceDisconnected = callback;
}

void BleDeviceManager::setOnDeviceDiscovered(DeviceDiscoveredCallback callback) {
    onDeviceDiscovered = callback;
}

void BleDeviceManager::saveKnownDevices() {
    Preferences prefs;
    if (!prefs.begin("ble-devices", false)) {
        LOG_ERROR(TAG, "Failed to open preferences for saving");
        return;
    }

    prefs.clear();

    size_t count = 0;
    for (size_t i = 0; i < BLE_MAX_KNOWN_DEVICES; i++) {
        if (!knownDevices[i].valid) continue;

        String prefix = "dev" + String(count) + "_";
        prefs.putString((prefix + "addr").c_str(), knownDevices[i].address);
        prefs.putString((prefix + "name").c_str(), knownDevices[i].name);
        prefs.putString((prefix + "icon").c_str(), knownDevices[i].icon);
        prefs.putBool((prefix + "auto").c_str(), knownDevices[i].autoConnect);
        count++;
    }

    prefs.putUInt("count", count);
    prefs.end();
    LOG_INFO(TAG, "Saved %d known devices", count);
}

void BleDeviceManager::loadKnownDevices() {
    Preferences prefs;
    if (!prefs.begin("ble-devices", true)) {
        LOG_INFO(TAG, "No saved devices found");
        return;
    }

    // Clear all slots
    for (auto& dev : knownDevices) {
        dev.clear();
    }

    uint32_t count = prefs.getUInt("count", 0);
    size_t loaded = 0;

    for (uint32_t i = 0; i < count && loaded < BLE_MAX_KNOWN_DEVICES; i++) {
        String prefix = "dev" + String(i) + "_";
        String addr = prefs.getString((prefix + "addr").c_str(), "");

        if (!addr.isEmpty()) {
            knownDevices[loaded].set(
                addr.c_str(),
                prefs.getString((prefix + "name").c_str(), "").c_str(),
                prefs.getString((prefix + "icon").c_str(), "generic").c_str(),
                prefs.getBool((prefix + "auto").c_str(), true)
            );
            knownDevices[loaded].lastSeen = 0;
            loaded++;
        }
    }

    prefs.end();
    LOG_INFO(TAG, "Loaded %d known devices", loaded);
}

void BleDeviceManager::startActiveScan() {
    // Clear scan results
    for (auto& result : lastScanResults) {
        result.clear();
    }

    esp_coex_preference_set(ESP_COEX_PREFER_BT);

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&activeScanCallbacks);
    scan->setInterval(100);
    scan->setWindow(99);
    scan->setActiveScan(true);
    scan->setDuplicateFilter(true);  // Filter duplicates - we only need to see each device once
    scan->setFilterPolicy(BLE_HCI_SCAN_FILT_NO_WL);
    scan->clearResults();

    if (scan->start(ACTIVE_SCAN_DURATION_MS, false, false)) {
        activeScanRunning = true;
        LOG_INFO(TAG, "Active scan started (%lu ms)", ACTIVE_SCAN_DURATION_MS);
    } else {
        LOG_ERROR(TAG, "Failed to start active scan");
        startBackgroundScan();
    }
}

void BleDeviceManager::stopActiveScan() {
    if (activeScanRunning) {
        NimBLEDevice::getScan()->stop();
        activeScanRunning = false;
        esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);
        LOG_INFO(TAG, "Active scan stopped");
    }
}

void BleDeviceManager::startBackgroundScan() {
    if (backgroundScanRunning || activeScanRunning) return;
    if (countValid(knownDevices) == 0) {
        LOG_INFO(TAG, "No known devices, skipping background scan");
        return;
    }

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&backgroundScanCallbacks);
    scan->setInterval(160);
    scan->setWindow(48);
    scan->setActiveScan(false);
    scan->setDuplicateFilter(false);  // Don't filter - ensures we see device even if it advertised before scan started
    scan->setFilterPolicy(BLE_HCI_SCAN_FILT_USE_WL);
    scan->clearResults();

    if (scan->start(0, false, false)) {
        backgroundScanRunning = true;
        LOG_INFO(TAG, "Background scan started (whitelist: %d)", NimBLEDevice::getWhiteListCount());
    } else {
        LOG_ERROR(TAG, "Failed to start background scan");
    }
}

void BleDeviceManager::stopBackgroundScan() {
    if (backgroundScanRunning) {
        NimBLEDevice::getScan()->stop();
        backgroundScanRunning = false;
        LOG_INFO(TAG, "Background scan stopped");
    }
}

void BleDeviceManager::updateWhitelist() {
    // Clear whitelist with safety limit to prevent infinite loop
    int count = static_cast<int>(NimBLEDevice::getWhiteListCount());
    for (int i = 0; i < count && i < 32; i++) {
        NimBLEAddress addr = NimBLEDevice::getWhiteListAddress(0);
        if (!NimBLEDevice::whiteListRemove(addr)) {
            LOG_WARN(TAG, "Failed to remove from whitelist: %s", addr.toString().c_str());
            break;  // Stop on failure to avoid spam
        }
    }

    for (const auto& dev : knownDevices) {
        if (!dev.valid || !dev.autoConnect) continue;

        NimBLEAddress addr(std::string(dev.address), BLE_ADDR_PUBLIC);
        if (NimBLEDevice::whiteListAdd(addr)) {
            LOG_INFO(TAG, "Added to whitelist: %s", dev.address);
        } else {
            NimBLEAddress addrRandom(std::string(dev.address), BLE_ADDR_RANDOM);
            if (NimBLEDevice::whiteListAdd(addrRandom)) {
                LOG_INFO(TAG, "Added to whitelist (random): %s", dev.address);
            }
        }
    }

    LOG_INFO(TAG, "Whitelist updated: %d entries", NimBLEDevice::getWhiteListCount());
}

void BleDeviceManager::handleScanResults() {
    // Results are handled in the scan callbacks
}

void BleDeviceManager::cleanupDisconnectedClients() {
    for (size_t i = 0; i < BLE_MAX_CONNECTED_DEVICES; i++) {
        auto& dev = connectedDevices[i];
        if (!dev.valid) continue;

        uint16_t connHandle = dev.client->getConnHandle();
        bool isConnected = dev.client->isConnected();

        if (!isConnected && connHandle != INVALID_CONN_HANDLE) {
            LOG_INFO(TAG, "Cleanup: removing disconnected client (handle=%d)", connHandle);
            dev.userData = nullptr;

            for (auto& cb : callbackPool) {
                if (cb.inUse && addressEquals(cb.address, dev.device.address)) {
                    cb.clear();
                    break;
                }
            }

            NimBLEDevice::deleteClient(dev.client);
            dev.clear();
        }
    }
}