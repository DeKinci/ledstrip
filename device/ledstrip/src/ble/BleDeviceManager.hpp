#ifndef GARLAND_BLE_DEVICE_MANAGER_H
#define GARLAND_BLE_DEVICE_MANAGER_H

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <array>

// Configurable limits - can be overridden before including this header
#ifndef BLE_MAX_KNOWN_DEVICES
#define BLE_MAX_KNOWN_DEVICES 8
#endif

#ifndef BLE_MAX_CONNECTED_DEVICES
#define BLE_MAX_CONNECTED_DEVICES 4
#endif

#ifndef BLE_MAX_SCAN_RESULTS
#define BLE_MAX_SCAN_RESULTS 32
#endif

// Fixed-size device info (no heap allocation)
struct KnownDevice {
    char address[18];   // "XX:XX:XX:XX:XX:XX\0"
    char name[32];
    char icon[16];
    bool autoConnect;
    uint32_t lastSeen;
    bool valid;         // Slot in use

    KnownDevice() : autoConnect(true), lastSeen(0), valid(false) {
        address[0] = '\0';
        name[0] = '\0';
        icon[0] = '\0';
    }

    void set(const char* addr, const char* n, const char* ic, bool ac = true) {
        strncpy(address, addr, sizeof(address) - 1);
        address[sizeof(address) - 1] = '\0';
        strncpy(name, n ? n : "", sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        strncpy(icon, ic ? ic : "generic", sizeof(icon) - 1);
        icon[sizeof(icon) - 1] = '\0';
        autoConnect = ac;
        lastSeen = millis();
        valid = true;
    }

    void clear() {
        valid = false;
        address[0] = '\0';
        name[0] = '\0';
        icon[0] = '\0';
    }
};

struct ConnectedDevice {
    NimBLEClient* client;
    KnownDevice device;
    void* userData;
    bool valid;         // Slot in use

    ConnectedDevice() : client(nullptr), userData(nullptr), valid(false) {}

    void set(NimBLEClient* c, const KnownDevice& d) {
        client = c;
        device = d;
        userData = nullptr;
        valid = true;
    }

    void clear() {
        client = nullptr;
        userData = nullptr;
        valid = false;
    }
};

// Callback types (function pointers - no heap allocation)
using DeviceConnectedCallback = void(*)(ConnectedDevice&);
using DeviceDisconnectedCallback = void(*)(const char* address, int reason);
using DeviceDiscoveredCallback = void(*)(const NimBLEAdvertisedDevice*);

class BleDeviceManager {
public:
    static void init();
    static void loop();

    // Device management
    static bool addKnownDevice(const char* address, const char* name = "",
                              const char* icon = "generic", bool autoConnect = true);
    static bool removeKnownDevice(const char* address);
    static const std::array<KnownDevice, BLE_MAX_KNOWN_DEVICES>& getKnownDevices();
    static size_t getKnownDeviceCount();
    static bool isKnownDevice(const char* address);

    // Connection management
    static bool connectToDevice(const char* address);
    static bool disconnectDevice(const char* address);
    static ConnectedDevice* getConnectedDevice(const char* address);
    static const std::array<ConnectedDevice, BLE_MAX_CONNECTED_DEVICES>& getConnectedDevices();
    static size_t getConnectedDeviceCount();

    // Scanning control
    static void triggerScanNow();         // Manual active scan for discovery
    static bool isScanning();             // True during manual active scan
    static const std::array<KnownDevice, BLE_MAX_SCAN_RESULTS>& getLastScanResults();
    static size_t getLastScanResultCount();

    // Callbacks
    static void setOnDeviceConnected(DeviceConnectedCallback callback);
    static void setOnDeviceDisconnected(DeviceDisconnectedCallback callback);
    static void setOnDeviceDiscovered(DeviceDiscoveredCallback callback);

    // Storage
    static void saveKnownDevices();
    static void loadKnownDevices();

    // Background scan control (public for callbacks)
    static void startBackgroundScan();    // Passive whitelist scan
    static void stopBackgroundScan();

private:
    static void startActiveScan();        // For manual discovery
    static void stopActiveScan();
    static void updateWhitelist();        // Sync whitelist with known devices
    static void handleScanResults();
    static void cleanupDisconnectedClients();
};

#endif
