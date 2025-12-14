#include "BleDeviceManager.hpp"
#include "BleButton.hpp"
#include <Preferences.h>

namespace {

// Configuration
static constexpr uint32_t SCAN_DURATION_MS = 5000;
static constexpr uint32_t DEFAULT_SCAN_INTERVAL_MS = 30000;
static constexpr uint32_t CONNECTION_TIMEOUT_MS = 5000;

// State
static std::vector<KnownDevice> knownDevices;
static std::vector<ConnectedDevice*> connectedDevices;
static bool periodicScanningEnabled = false;
static uint32_t scanIntervalMs = DEFAULT_SCAN_INTERVAL_MS;
static uint32_t lastScanTime = 0;
static bool currentlyScanning = false;
static std::vector<NimBLEAddress> devicesToConnect;
static std::vector<KnownDevice> lastScanResults;

// Callbacks
static DeviceConnectedCallback onDeviceConnected = nullptr;
static DeviceDisconnectedCallback onDeviceDisconnected = nullptr;
static DeviceDiscoveredCallback onDeviceDiscovered = nullptr;

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

// Client callbacks for connection management
class DeviceClientCallbacks : public NimBLEClientCallbacks {
public:
    DeviceClientCallbacks(const String& addr) : address(addr) {}

    void onConnect(NimBLEClient* client) override {
        Serial.printf("[BleDeviceManager] Connected to %s\n", address.c_str());

        // IMPORTANT: Must call secureConnection like the original prototype
        client->secureConnection(true);

        // Find the connected device and initialize button handler
        for (auto* dev : connectedDevices) {
            if (dev->client == client) {
                // Create BleButton instance for this device
                BleButton* btn = new BleButton(client);
                dev->userData = btn;
                Serial.printf("[BleDeviceManager] Initialized button for %s\n", dev->device.name.c_str());

                // Trigger callback if set
                if (onDeviceConnected) {
                    onDeviceConnected(*dev);
                }
                break;
            }
        }
    }

    void onDisconnect(NimBLEClient* client, int reason) override {
        Serial.printf("[BleDeviceManager] Disconnected from %s (reason: %d)\n",
                     address.c_str(), reason);

        if (onDeviceDisconnected) {
            onDeviceDisconnected(address, reason);
        }

        // Remove from connected devices list and cleanup userData
        for (auto it = connectedDevices.begin(); it != connectedDevices.end(); ++it) {
            if ((*it)->client == client) {
                // Cleanup userData if it exists (e.g., BleButton instance)
                if ((*it)->userData != nullptr) {
                    delete static_cast<BleButton*>((*it)->userData);
                    (*it)->userData = nullptr;
                }
                delete *it;
                connectedDevices.erase(it);
                break;
            }
        }
    }

private:
    String address;
};

// HID service UUID - we only care about button devices
static const NimBLEUUID HID_SERVICE_UUID(uint16_t(0x1812));

// Scan callbacks
class DeviceScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* device) override {
        String address = device->getAddress().toString().c_str();

        // Only process HID devices (buttons, keyboards, gamepads, etc.)
        if (!device->isAdvertisingService(HID_SERVICE_UUID)) {
            return;
        }

        // Add to scan results
        bool found = false;
        for (auto& result : lastScanResults) {
            if (result.address.equalsIgnoreCase(address)) {
                found = true;
                result.lastSeen = millis();
                if (!device->getName().empty()) {
                    result.name = device->getName().c_str();
                }
                break;
            }
        }

        if (!found) {
            String name = device->getName().empty() ? "" : device->getName().c_str();
            String icon = getIconNameFromAppearance(device->getAppearance());
            lastScanResults.push_back(KnownDevice(address, name, icon, false));
        }

        // Call discovery callback
        if (onDeviceDiscovered) {
            onDeviceDiscovered(device);
        }

        // Check if this is a known device
        for (auto& known : knownDevices) {
            if (known.address.equalsIgnoreCase(address)) {
                known.lastSeen = millis();

                // Update name if we have a better one
                if (!device->getName().empty() && known.name.isEmpty()) {
                    known.name = device->getName().c_str();
                }

                // Check if we should auto-connect
                if (known.autoConnect) {
                    bool alreadyConnected = false;
                    bool alreadyQueued = false;

                    // Check if already connected
                    for (auto* conn : connectedDevices) {
                        if (conn->device.address.equalsIgnoreCase(address)) {
                            alreadyConnected = true;
                            break;
                        }
                    }

                    // Check if already queued for connection
                    for (auto& addr : devicesToConnect) {
                        if (addr == device->getAddress()) {
                            alreadyQueued = true;
                            break;
                        }
                    }

                    if (!alreadyConnected && !alreadyQueued) {
                        Serial.printf("[BleDeviceManager] Found known device: %s (%s)\n",
                                    known.name.c_str(), address.c_str());
                        devicesToConnect.push_back(device->getAddress());
                    }
                }
                break;
            }
        }
    }

    void onScanEnd(const NimBLEScanResults& results, int reason) override {
        Serial.printf("[BleDeviceManager] Scan ended (found %d HID devices, reason: %d)\n",
                     lastScanResults.size(), reason);
        currentlyScanning = false;
    }
};

static DeviceScanCallbacks scanCallbacks;

}  // namespace

void BleDeviceManager::init() {
    NimBLEDevice::init("SmartGarland");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
    NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);

    loadKnownDevices();
    Serial.println("[BleDeviceManager] Initialized");
}

void BleDeviceManager::loop() {
    // Handle periodic scanning
    if (periodicScanningEnabled && !currentlyScanning) {
        if (millis() - lastScanTime >= scanIntervalMs) {
            triggerScanNow();
        }
    }

    // Process connection queue
    if (!devicesToConnect.empty() && !currentlyScanning) {
        NimBLEAddress addr = devicesToConnect.back();
        devicesToConnect.pop_back();

        String addrStr = addr.toString().c_str();
        Serial.printf("[BleDeviceManager] Attempting to connect to %s\n", addrStr.c_str());

        // Find the known device
        for (auto& known : knownDevices) {
            if (known.address.equalsIgnoreCase(addrStr)) {
                // Try to connect
                NimBLEClient* client = NimBLEDevice::getDisconnectedClient();
                if (!client) {
                    client = NimBLEDevice::createClient();
                }

                if (client) {
                    // IMPORTANT: Use false for auto-delete like the original prototype
                    client->setClientCallbacks(new DeviceClientCallbacks(addrStr), false);
                    client->setConnectionParams(12, 12, 0, 150);
                    client->setConnectTimeout(CONNECTION_TIMEOUT_MS);

                    // IMPORTANT: Use same connect parameters as original: (addr, deleteOnDisconnect=false, asyncConnect=true, exchangeMTU=true)
                    if (client->connect(addr, false, true, true)) {
                        // Add to connected devices
                        ConnectedDevice* connDev = new ConnectedDevice(client, known);
                        connectedDevices.push_back(connDev);
                    } else {
                        Serial.printf("[BleDeviceManager] Failed to connect to %s\n", addrStr.c_str());
                        NimBLEDevice::deleteClient(client);
                    }
                }
                break;
            }
        }
    }

    // Call loop on all connected button devices
    for (auto* device : connectedDevices) {
        if (device->userData != nullptr) {
            BleButton* btn = static_cast<BleButton*>(device->userData);
            btn->loop();
        }
    }

    // Cleanup disconnected clients
    cleanupDisconnectedClients();
}

bool BleDeviceManager::addKnownDevice(const String& address, const String& name,
                                     const String& icon, bool autoConnect) {
    // Check if already exists
    for (auto& dev : knownDevices) {
        if (dev.address.equalsIgnoreCase(address)) {
            // Update existing
            dev.name = name;
            dev.icon = icon;
            dev.autoConnect = autoConnect;
            saveKnownDevices();
            return true;
        }
    }

    // Add new
    knownDevices.push_back(KnownDevice(address, name, icon, autoConnect));
    saveKnownDevices();
    Serial.printf("[BleDeviceManager] Added known device: %s (%s)\n", name.c_str(), address.c_str());
    return true;
}

bool BleDeviceManager::removeKnownDevice(const String& address) {
    for (auto it = knownDevices.begin(); it != knownDevices.end(); ++it) {
        if (it->address.equalsIgnoreCase(address)) {
            // Disconnect if currently connected
            disconnectDevice(address);

            knownDevices.erase(it);
            saveKnownDevices();
            Serial.printf("[BleDeviceManager] Removed known device: %s\n", address.c_str());
            return true;
        }
    }
    return false;
}

std::vector<KnownDevice> BleDeviceManager::getKnownDevices() {
    return knownDevices;
}

bool BleDeviceManager::isKnownDevice(const String& address) {
    for (auto& dev : knownDevices) {
        if (dev.address.equalsIgnoreCase(address)) {
            return true;
        }
    }
    return false;
}

bool BleDeviceManager::connectToDevice(const String& address) {
    // Check if already connected
    for (auto* conn : connectedDevices) {
        if (conn->device.address.equalsIgnoreCase(address)) {
            Serial.printf("[BleDeviceManager] Already connected to %s\n", address.c_str());
            return true;
        }
    }

    // Add to connection queue if it's a known device
    for (auto& known : knownDevices) {
        if (known.address.equalsIgnoreCase(address)) {
            NimBLEAddress addr(std::string(address.c_str()), BLE_ADDR_PUBLIC);
            devicesToConnect.push_back(addr);
            return true;
        }
    }

    Serial.printf("[BleDeviceManager] Cannot connect to unknown device: %s\n", address.c_str());
    return false;
}

bool BleDeviceManager::disconnectDevice(const String& address) {
    for (auto it = connectedDevices.begin(); it != connectedDevices.end(); ++it) {
        if ((*it)->device.address.equalsIgnoreCase(address)) {
            (*it)->client->disconnect();
            NimBLEDevice::deleteClient((*it)->client);
            delete *it;
            connectedDevices.erase(it);
            return true;
        }
    }
    return false;
}

ConnectedDevice* BleDeviceManager::getConnectedDevice(const String& address) {
    for (auto* conn : connectedDevices) {
        if (conn->device.address.equalsIgnoreCase(address)) {
            return conn;
        }
    }
    return nullptr;
}

std::vector<ConnectedDevice*> BleDeviceManager::getConnectedDevices() {
    return connectedDevices;
}

void BleDeviceManager::startPeriodicScanning(uint32_t intervalMs) {
    periodicScanningEnabled = true;
    scanIntervalMs = intervalMs;
    lastScanTime = 0;  // Trigger immediate scan
    Serial.printf("[BleDeviceManager] Periodic scanning enabled (interval: %d ms)\n", intervalMs);
}

void BleDeviceManager::stopPeriodicScanning() {
    periodicScanningEnabled = false;
    stopScan();
    Serial.println("[BleDeviceManager] Periodic scanning disabled");
}

void BleDeviceManager::triggerScanNow() {
    if (!currentlyScanning) {
        startScan();
        lastScanTime = millis();
    }
}

bool BleDeviceManager::isScanning() {
    return currentlyScanning;
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
        Serial.println("[BleDeviceManager] Failed to open preferences for saving");
        return;
    }

    // Clear existing data
    prefs.clear();

    // Save count
    prefs.putUInt("count", knownDevices.size());

    // Save each device
    for (size_t i = 0; i < knownDevices.size(); i++) {
        String prefix = "dev" + String(i) + "_";
        prefs.putString((prefix + "addr").c_str(), knownDevices[i].address);
        prefs.putString((prefix + "name").c_str(), knownDevices[i].name);
        prefs.putString((prefix + "icon").c_str(), knownDevices[i].icon);
        prefs.putBool((prefix + "auto").c_str(), knownDevices[i].autoConnect);
    }

    prefs.end();
    Serial.printf("[BleDeviceManager] Saved %d known devices\n", knownDevices.size());
}

void BleDeviceManager::loadKnownDevices() {
    Preferences prefs;
    if (!prefs.begin("ble-devices", true)) {
        Serial.println("[BleDeviceManager] No saved devices found");
        return;
    }

    uint32_t count = prefs.getUInt("count", 0);
    knownDevices.clear();

    for (uint32_t i = 0; i < count; i++) {
        String prefix = "dev" + String(i) + "_";
        String addr = prefs.getString((prefix + "addr").c_str(), "");

        if (!addr.isEmpty()) {
            KnownDevice dev;
            dev.address = addr;
            dev.name = prefs.getString((prefix + "name").c_str(), "");
            dev.icon = prefs.getString((prefix + "icon").c_str(), "generic");
            dev.autoConnect = prefs.getBool((prefix + "auto").c_str(), true);
            dev.lastSeen = 0;
            knownDevices.push_back(dev);
        }
    }

    prefs.end();
    Serial.printf("[BleDeviceManager] Loaded %d known devices\n", knownDevices.size());
}

std::vector<KnownDevice> BleDeviceManager::getLastScanResults() {
    return lastScanResults;
}

void BleDeviceManager::startScan() {
    // Clear previous scan results
    lastScanResults.clear();

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&scanCallbacks);
    scan->setInterval(80);
    scan->setWindow(40);
    scan->setActiveScan(true);
    scan->setDuplicateFilter(1); // Use 1 like the original prototype, not true

    // IMPORTANT: Use same scan parameters as original: (duration, is_continue, restart_on_end)
    if (scan->start(SCAN_DURATION_MS / 1000, false, true)) {
        currentlyScanning = true;
        Serial.println("[BleDeviceManager] BLE scan started");
    } else {
        Serial.println("[BleDeviceManager] Failed to start scan");
    }
}

void BleDeviceManager::stopScan() {
    if (currentlyScanning) {
        NimBLEDevice::getScan()->stop();
        currentlyScanning = false;
        Serial.println("[BleDeviceManager] BLE scan stopped");
    }
}

void BleDeviceManager::handleScanResults() {
    // Results are handled in the scan callbacks
}

void BleDeviceManager::cleanupDisconnectedClients() {
    for (auto it = connectedDevices.begin(); it != connectedDevices.end();) {
        if (!(*it)->client->isConnected()) {
            // Cleanup userData if it exists
            if ((*it)->userData != nullptr) {
                delete static_cast<BleButton*>((*it)->userData);
                (*it)->userData = nullptr;
            }
            NimBLEDevice::deleteClient((*it)->client);
            delete *it;
            it = connectedDevices.erase(it);
        } else {
            ++it;
        }
    }
}
