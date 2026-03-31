#ifndef MICROPROTO_TRANSPORT_BLE_SERVER_H
#define MICROPROTO_TRANSPORT_BLE_SERVER_H

#include <NimBLEDevice.h>
#include <array>
#include <atomic>
#include "MicroProtoTransport.h"
#include "BleFragmentation.h"

// Forward declare
namespace MicroProto { class MicroProtoController; }

namespace MicroProto {

/**
 * BLE transport for MicroProto.
 * Handles NimBLE GATT, fragmentation/reassembly, cross-task safety.
 * All protocol logic lives in MicroProtoController.
 */
class MicroProtoBleServer : public MicroProtoTransport,
                            public NimBLEServerCallbacks,
                            public NimBLECharacteristicCallbacks {
public:
    static constexpr uint8_t MAX_CLIENTS = 3;
    static constexpr size_t RX_QUEUE_SIZE = 4;
    static constexpr uint8_t RX_BACKPRESSURE_RETRIES = 10;
    static constexpr size_t REASSEMBLY_SIZE = 4096;

    void begin(MicroProtoController* controller);
    void loop();
    uint8_t connectedClients();

    // MicroProtoTransport interface
    void send(uint8_t localClientId, const uint8_t* data, size_t len) override;
    uint8_t maxClients() const override { return MAX_CLIENTS; }
    bool isClientConnected(uint8_t localClientId) const override;
    uint32_t maxPacketSize(uint8_t localClientId) const override;

    Capabilities capabilities() const override {
        return { .requiresBleExposed = true };
    }

    // NimBLEServerCallbacks
    void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override;
    void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override;
    void onMTUChange(uint16_t mtu, NimBLEConnInfo& connInfo) override;

    // NimBLECharacteristicCallbacks
    void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo) override;

private:
    MicroProtoController* _controller = nullptr;
    uint8_t _clientIdOffset = 0;

    NimBLEServer* _server = nullptr;
    NimBLECharacteristic* _txChar = nullptr;
    NimBLECharacteristic* _rxChar = nullptr;

    struct BleClient {
        uint16_t connHandle = 0;
        uint16_t mtu = 23;
        bool ready = false;
        bool valid = false;
        std::atomic<bool> messageReady{false};
        BleReassembler<REASSEMBLY_SIZE> reassembler;
    };
    std::array<BleClient, MAX_CLIENTS> _clients = {};

    struct RxSignal { uint8_t clientId = 0; };
    std::array<RxSignal, RX_QUEUE_SIZE> _rxQueue = {};
    portMUX_TYPE _clientsMux = portMUX_INITIALIZER_UNLOCKED;
    std::atomic<uint8_t> _rxHead{0};
    std::atomic<uint8_t> _rxTail{0};
    std::atomic<uint8_t> _connCount{0};

    uint8_t findClientSlot(uint16_t connHandle);
    uint8_t allocClientSlot(uint16_t connHandle);
    void freeClientSlot(uint16_t connHandle);
    uint16_t maxPayloadForClient(uint8_t clientIdx) const;
    void sendToClient(uint8_t clientIdx, const uint8_t* data, size_t length);
    void processRxQueue();
    void startAdvertising();
    bool _advertisingConfigured = false;
};

} // namespace MicroProto

#endif // MICROPROTO_TRANSPORT_BLE_SERVER_H
