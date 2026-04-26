#ifndef MICROPROTO_TRANSPORT_BLE_SERVER_H
#define MICROPROTO_TRANSPORT_BLE_SERVER_H

#include <MicroProtoTransport.h>
#include <BleMessageService.h>

// Forward declare
namespace MicroProto { class MicroProtoController; }

namespace MicroProto {

/**
 * BLE transport for MicroProto.
 * Thin layer on BleMessageService — just wires messages to MicroProtoController.
 */
class MicroProtoBleServer : public MicroProtoTransport,
                            public MicroBLE::MessageHandler {
public:
    static constexpr uint8_t MAX_CLIENTS = MICROBLE_MAX_CLIENTS;

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

    // MicroBLE::MessageHandler
    void onMessage(uint8_t slot, const uint8_t* data, size_t len) override;
    void onConnect(uint8_t slot) override;
    void onDisconnect(uint8_t slot) override;

private:
    MicroProtoController* _controller = nullptr;
    uint8_t _clientIdOffset = 0;
    MicroBLE::BleMessageService _msg;
};

} // namespace MicroProto

#endif // MICROPROTO_TRANSPORT_BLE_SERVER_H
