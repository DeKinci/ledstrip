#ifndef MICROPROTO_TRANSPORT_SERVER_H
#define MICROPROTO_TRANSPORT_SERVER_H

#include <WebSocketsServer.h>
#include <array>
#include "../wire/Buffer.h"
#include "../wire/OpCode.h"
#include "../wire/PropertyUpdate.h"
#include "../messages/Hello.h"
#include "../messages/Error.h"
#include "../messages/Schema.h"
#include "../messages/MessageRouter.h"
#include "../PropertyBase.h"
#include "../PropertySystem.h"

namespace MicroProto {

/**
 * MicroProtoServer - WebSocket transport for MicroProto protocol
 *
 * Handles:
 * - Client connections with HELLO handshake
 * - Schema and property value sync on connect
 * - Property update broadcasts (rate-limited to ~15 Hz)
 * - Incoming property updates from clients
 */
class MicroProtoServer : public MessageHandler {
public:
    static constexpr size_t TX_BUFFER_SIZE = 4096;
    static constexpr size_t MAX_CLIENTS = 4;
    static constexpr uint32_t BROADCAST_INTERVAL_MS = 67;  // ~15 Hz

    MicroProtoServer(uint16_t port = 81)
        : _ws(port), _router(this), _nextSessionId(1) {}

    void begin();
    void loop();
    uint8_t connectedClients();
    void broadcastProperty(const PropertyBase* prop);
    void broadcastAllProperties();

    // MessageHandler interface
    void onHello(const HelloRequest& hello) override;
    void onPropertyUpdate(uint8_t propertyId, const void* value, size_t size) override;
    void onError(const ErrorMessage& error) override;
    void onPing(uint32_t payload) override;
    void onConstraintViolation(uint8_t propertyId, ErrorCode code) override;

private:
    WebSocketsServer _ws;
    MessageRouter _router;
    uint32_t _nextSessionId;
    uint8_t _currentClient = 0;
    std::array<bool, MAX_CLIENTS> _clientReady = {};
    DirtySet _pendingBroadcast;
    uint32_t _lastBroadcastTime = 0;

    void handleEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
    void sendHelloResponse(uint8_t clientNum);
    void sendSchema(uint8_t clientNum);
    void sendAllPropertyValues(uint8_t clientNum);
    void sendError(uint8_t clientNum, const ErrorMessage& error);
    void sendPong(uint8_t clientNum, uint32_t payload);
    void broadcastPropertyExcept(const PropertyBase* prop, uint8_t excludeClient);
    void onPropertiesChanged(const DirtySet& dirty);
    void flushBroadcasts();
};

} // namespace MicroProto

#endif // MICROPROTO_TRANSPORT_SERVER_H
