#ifndef GARLAND_BLE_DEVICE_MANAGER_H
#define GARLAND_BLE_DEVICE_MANAGER_H

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <vector>
#include <functional>

struct KnownDevice {
    String address;
    String name;
    String icon;
    bool autoConnect;
    uint32_t lastSeen;

    KnownDevice() : autoConnect(true), lastSeen(0) {}
    KnownDevice(const String& addr, const String& n, const String& ic, bool ac = true)
        : address(addr), name(n), icon(ic), autoConnect(ac), lastSeen(millis()) {}
};

struct ConnectedDevice {
    NimBLEClient* client;
    KnownDevice device;
    void* userData;

    ConnectedDevice(NimBLEClient* c, const KnownDevice& d)
        : client(c), device(d), userData(nullptr) {}
};

// Callback types
using DeviceConnectedCallback = std::function<void(ConnectedDevice&)>;
using DeviceDisconnectedCallback = std::function<void(const String& address, int reason)>;
using DeviceDiscoveredCallback = std::function<void(const NimBLEAdvertisedDevice*)>;

class BleDeviceManager {
public:
    static void init();
    static void loop();

    // Device management
    static bool addKnownDevice(const String& address, const String& name = "",
                              const String& icon = "generic", bool autoConnect = true);
    static bool removeKnownDevice(const String& address);
    static std::vector<KnownDevice> getKnownDevices();
    static bool isKnownDevice(const String& address);

    // Connection management
    static bool connectToDevice(const String& address);
    static bool disconnectDevice(const String& address);
    static ConnectedDevice* getConnectedDevice(const String& address);
    static std::vector<ConnectedDevice*> getConnectedDevices();

    // Scanning control
    static void startPeriodicScanning(uint32_t intervalMs = 30000);
    static void stopPeriodicScanning();
    static void triggerScanNow();
    static bool isScanning();
    static std::vector<KnownDevice> getLastScanResults();

    // Callbacks
    static void setOnDeviceConnected(DeviceConnectedCallback callback);
    static void setOnDeviceDisconnected(DeviceDisconnectedCallback callback);
    static void setOnDeviceDiscovered(DeviceDiscoveredCallback callback);

    // Storage
    static void saveKnownDevices();
    static void loadKnownDevices();

private:
    static void startScan();
    static void stopScan();
    static void handleScanResults();
    static void cleanupDisconnectedClients();
};

#endif
