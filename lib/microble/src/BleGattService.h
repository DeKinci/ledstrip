#pragma once

#include <stdint.h>
#include <stddef.h>

#ifndef NATIVE_TEST
#include <NimBLEDevice.h>
#include <array>

namespace MicroBLE {

// ---------------------------------------------------------------------------
// Configuration for a GATT service
// ---------------------------------------------------------------------------
struct GattConfig {
    const char* serviceUUID;
    const char* rxUUID;            // Client -> device (WRITE | WRITE_NR)
    const char* txUUID;            // Device -> client (NOTIFY or INDICATE)
    bool txIndicate = false;       // true = INDICATE, false = NOTIFY
    uint8_t maxClients = 3;
};

// ---------------------------------------------------------------------------
// Handler interface — implemented by protocol-specific transport layers
// ---------------------------------------------------------------------------
class GattHandler {
public:
    virtual ~GattHandler() = default;
    virtual void onConnect(uint8_t slot) = 0;
    virtual void onDisconnect(uint8_t slot) = 0;
    virtual void onMTUChange(uint8_t slot, uint16_t mtu) = 0;
    // Called on NimBLE task — keep fast, queue if needed
    virtual void onWrite(uint8_t slot, const uint8_t* data, size_t len) = 0;
};

// ---------------------------------------------------------------------------
// BleGattService — one per protocol (MicroProto, Matter, etc.)
//
// Manages a single GATT service with RX/TX characteristics and
// per-connection client slot tracking with spinlock safety.
// ---------------------------------------------------------------------------
static constexpr uint8_t MAX_GATT_CLIENTS = 8;

class BleGattService : public NimBLECharacteristicCallbacks {
public:
    void begin(GattHandler* handler, const GattConfig& config);

    // Raw BLE send (notify or indicate based on config)
    bool send(uint8_t slot, const uint8_t* data, size_t len);

    // Client info
    uint16_t mtu(uint8_t slot) const;
    bool isConnected(uint8_t slot) const;
    uint8_t connectedCount() const;
    uint16_t connHandle(uint8_t slot) const;

private:
    // Called by MicroBLE's server callback mux
    friend class ServerCallbackMux;
    void onServerConnect(uint16_t connHandle);
    void onServerDisconnect(uint16_t connHandle);
    void onServerMTUChange(uint16_t connHandle, uint16_t mtu);

    // NimBLECharacteristicCallbacks
    void onWrite(NimBLECharacteristic* ch, NimBLEConnInfo& conn) override;

    // Client slot management (called under spinlock)
    uint8_t findSlot(uint16_t connHandle) const;
    uint8_t allocSlot(uint16_t connHandle);
    void freeSlot(uint16_t connHandle);

    struct ClientSlot {
        uint16_t connHandle = 0;
        uint16_t mtu = 23;
        bool valid = false;
    };

    GattHandler* _handler = nullptr;
    GattConfig _config = {};
    std::array<ClientSlot, MAX_GATT_CLIENTS> _clients = {};
    NimBLECharacteristic* _txChar = nullptr;
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
    uint8_t _connCount = 0;
};

} // namespace MicroBLE

#endif // NATIVE_TEST
