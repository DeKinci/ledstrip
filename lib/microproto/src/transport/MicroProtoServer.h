#ifndef MICROPROTO_TRANSPORT_SERVER_H
#define MICROPROTO_TRANSPORT_SERVER_H

#include <WebSocketsServer.h>
#include "MicroProtoTransport.h"

// Forward declare to avoid circular includes
namespace MicroProto { class MicroProtoController; }

#ifndef MICROPROTO_MAX_CLIENTS
#define MICROPROTO_MAX_CLIENTS 4
#endif

namespace MicroProto {

/**
 * WebSocket transport for MicroProto.
 * Thin wrapper — all protocol logic lives in MicroProtoController.
 */
class MicroProtoServer : public MicroProtoTransport {
public:
    static constexpr uint8_t MAX_CLIENTS = MICROPROTO_MAX_CLIENTS;

    MicroProtoServer(uint16_t port = 81) : _ws(port) {}

    void begin(MicroProtoController* controller);
    void loop();
    uint8_t connectedClients();

    // MicroProtoTransport interface
    void send(uint8_t localClientId, const uint8_t* data, size_t len) override;
    uint8_t maxClients() const override { return MAX_CLIENTS; }
    bool isClientConnected(uint8_t localClientId) const override { return localClientId < MAX_CLIENTS; }
    uint32_t maxPacketSize(uint8_t /*localClientId*/) const override { return 4096; }

private:
    WebSocketsServer _ws;
    MicroProtoController* _controller = nullptr;
    uint8_t _clientIdOffset = 0;

    void handleEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
};

} // namespace MicroProto

#endif // MICROPROTO_TRANSPORT_SERVER_H
