#ifndef MICROPROTO_TRANSPORT_BLE_SERVER_H
#define MICROPROTO_TRANSPORT_BLE_SERVER_H

#include <array>
#include <atomic>
#include "MicroProtoTransport.h"
#include "BleFragmentation.h"
#include <BleGattService.h>

// Forward declare
namespace MicroProto { class MicroProtoController; }

namespace MicroProto {

/**
 * BLE transport for MicroProto.
 * Uses MicroBLE::BleGattService for GATT plumbing.
 * Handles fragmentation/reassembly, cross-task safety, backpressure.
 * All protocol logic lives in MicroProtoController.
 */
class MicroProtoBleServer : public MicroProtoTransport,
                            public MicroBLE::GattHandler {
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

    // MicroBLE::GattHandler
    void onConnect(uint8_t slot) override;
    void onDisconnect(uint8_t slot) override;
    void onMTUChange(uint8_t slot, uint16_t mtu) override;
    void onWrite(uint8_t slot, const uint8_t* data, size_t len) override;

private:
    MicroProtoController* _controller = nullptr;
    uint8_t _clientIdOffset = 0;

    MicroBLE::BleGattService _gatt;

    struct ClientState {
        std::atomic<bool> messageReady{false};
        BleReassembler<REASSEMBLY_SIZE> reassembler;
    };
    std::array<ClientState, MAX_CLIENTS> _clients = {};

    struct RxSignal { uint8_t clientId = 0; };
    std::array<RxSignal, RX_QUEUE_SIZE> _rxQueue = {};
    std::atomic<uint8_t> _rxHead{0};
    std::atomic<uint8_t> _rxTail{0};

    uint16_t maxPayloadForClient(uint8_t clientIdx) const;
    void sendToClient(uint8_t clientIdx, const uint8_t* data, size_t length);
    void processRxQueue();
};

} // namespace MicroProto

#endif // MICROPROTO_TRANSPORT_BLE_SERVER_H
