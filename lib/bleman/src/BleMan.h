#ifndef BLEMAN_H
#define BLEMAN_H

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <HttpDispatcher.h>
#include <array>

#include "BleDriver.h"

// Configurable limits — override before including this header
#ifndef BLEMAN_MAX_KNOWN_DEVICES
#define BLEMAN_MAX_KNOWN_DEVICES 8
#endif

#ifndef BLEMAN_MAX_CONNECTED_DEVICES
#define BLEMAN_MAX_CONNECTED_DEVICES 4
#endif

#ifndef BLEMAN_MAX_SCAN_RESULTS
#define BLEMAN_MAX_SCAN_RESULTS 32
#endif

#ifndef BLEMAN_MAX_DRIVER_TYPES
#define BLEMAN_MAX_DRIVER_TYPES 8
#endif

// Fixed-size device info (no heap allocation)
struct BleKnownDevice {
    char address[18];   // "XX:XX:XX:XX:XX:XX\0"
    char name[32];
    char icon[16];
    char type[16];      // Driver type: "button", "sensor", etc.
    bool autoConnect;
    uint32_t lastSeen;
    bool valid;         // Slot in use

    BleKnownDevice() : autoConnect(true), lastSeen(0), valid(false) {
        address[0] = '\0';
        name[0] = '\0';
        icon[0] = '\0';
        type[0] = '\0';
    }

    void set(const char* addr, const char* n, const char* ic,
             const char* t, bool ac = true) {
        strncpy(address, addr, sizeof(address) - 1);
        address[sizeof(address) - 1] = '\0';
        strncpy(name, n ? n : "", sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        strncpy(icon, ic ? ic : "generic", sizeof(icon) - 1);
        icon[sizeof(icon) - 1] = '\0';
        strncpy(type, t ? t : "", sizeof(type) - 1);
        type[sizeof(type) - 1] = '\0';
        autoConnect = ac;
        lastSeen = millis();
        valid = true;
    }

    void clear() {
        valid = false;
        address[0] = '\0';
        name[0] = '\0';
        icon[0] = '\0';
        type[0] = '\0';
    }
};

// Connection slot state machine:
//   EMPTY → CONNECTING → CONNECTED → DISCONNECTING → CLEANUP → EMPTY
//                    ↘ CLEANUP (connect failed or removed while connecting)
enum class ConnState : uint8_t {
    EMPTY,          // Slot unused
    CONNECTING,     // connect() called, waiting for onConnect callback
    CONNECTED,      // onConnect fired, driver initialized
    DISCONNECTING,  // disconnect() called, waiting for onDisconnect callback
    CLEANUP,        // Ready to clean up (delete client, release driver)
};

struct BleConnectedDevice {
    NimBLEClient* client;
    BleKnownDevice device;
    BleDriver* driver;
    ConnState state;

    BleConnectedDevice() : client(nullptr), driver(nullptr), state(ConnState::EMPTY) {}

    bool isActive() const { return state != ConnState::EMPTY; }
    bool isConnected() const { return state == ConnState::CONNECTED; }
};

namespace BleMan {

struct DriverRegistration {
    char type[16];
    BleDriverFactory factory;
    bool valid;

    DriverRegistration() : factory(nullptr), valid(false) {
        type[0] = '\0';
    }
};

class BleManager {
public:
    BleManager(HttpDispatcher* dispatcher = nullptr);

    void begin();   // Load NVS, update whitelist, start background scan
    void loop();

    // Driver system
    void registerDriver(const char* type, BleDriverFactory factory);
    const std::array<DriverRegistration, BLEMAN_MAX_DRIVER_TYPES>& getDriverTypes() const;
    size_t getDriverTypeCount() const;

    // Device management
    bool addKnownDevice(const char* address, const char* name = "",
                        const char* icon = "generic", const char* type = "",
                        bool autoConnect = true);
    bool removeKnownDevice(const char* address);
    const std::array<BleKnownDevice, BLEMAN_MAX_KNOWN_DEVICES>& getKnownDevices() const;
    size_t getKnownDeviceCount() const;
    bool isKnownDevice(const char* address) const;

    // Connection management
    bool connectToDevice(const char* address);
    bool disconnectDevice(const char* address);
    BleConnectedDevice* getConnectedDevice(const char* address);
    const std::array<BleConnectedDevice, BLEMAN_MAX_CONNECTED_DEVICES>& getConnectedDevices() const;
    size_t getConnectedDeviceCount() const;

    // Scanning
    void triggerScanNow();
    bool isScanning() const;
    const std::array<BleKnownDevice, BLEMAN_MAX_SCAN_RESULTS>& getLastScanResults() const;
    size_t getLastScanResultCount() const;

    // Storage
    void saveKnownDevices();
    void loadKnownDevices();

    // Background scan control (public for callbacks)
    void startBackgroundScan();
    void stopBackgroundScan();

private:
    HttpDispatcher* _dispatcher;

    // Storage
    std::array<BleKnownDevice, BLEMAN_MAX_KNOWN_DEVICES> _knownDevices;
    std::array<BleConnectedDevice, BLEMAN_MAX_CONNECTED_DEVICES> _connectedDevices;
    std::array<BleKnownDevice, BLEMAN_MAX_SCAN_RESULTS> _lastScanResults;
    std::array<DriverRegistration, BLEMAN_MAX_DRIVER_TYPES> _driverTypes;

    // Connection queue (addresses to connect, processed one per loop)
    std::array<NimBLEAddress, BLEMAN_MAX_CONNECTED_DEVICES> _connectQueue;
    size_t _connectQueueCount = 0;

    // State
    bool _activeScanRunning = false;
    bool _backgroundScanRunning = false;

    // NimBLE callback classes — defined in .cpp
public:
    class DeviceClientCallbacks;
    class ActiveScanCallbacks;
    class BackgroundScanCallbacks;
private:

    // Internal
    void startActiveScan();
    void stopActiveScan();
    void updateWhitelist();
    void processConnectionQueue();
    void processSlots();  // Single unified slot state machine handler

    void cleanupSlot(size_t i);   // Release driver, delete client, reset slot
    void initDriver(size_t i);    // Create and init driver for connected device

    BleDriverFactory findDriverFactory(const char* type) const;
    void setupRoutes();

    // Helpers
    static bool addressEquals(const char* a, const char* b);

    template<typename T, size_t N>
    static int findEmptySlot(const std::array<T, N>& arr);

    template<typename T, size_t N>
    static size_t countValid(const std::array<T, N>& arr);

    static const char* getIconNameFromAppearance(uint16_t appearance);

    // Singleton for NimBLE callbacks (they need static context)
    static BleManager* _instance;

    friend class DeviceClientCallbacks;
    friend class ActiveScanCallbacks;
    friend class BackgroundScanCallbacks;
};

} // namespace BleMan

#endif // BLEMAN_H
