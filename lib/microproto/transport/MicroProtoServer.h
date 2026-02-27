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
#include "../messages/Resource.h"
#include "../PropertyBase.h"
#include "../PropertySystem.h"

// Configurable constants - override in platformio.ini build_flags
#ifndef MICROPROTO_TX_BUFFER_SIZE
#define MICROPROTO_TX_BUFFER_SIZE 4096
#endif

#ifndef MICROPROTO_MAX_CLIENTS
#define MICROPROTO_MAX_CLIENTS 4
#endif

#ifndef MICROPROTO_BROADCAST_INTERVAL_MS
#define MICROPROTO_BROADCAST_INTERVAL_MS 67  // ~15 Hz
#endif

namespace MicroProto {

// Sentinel: no client excluded from broadcast
static constexpr uint8_t NO_EXCLUDE = 0xFF;

/**
 * MicroProtoServer - WebSocket transport for MicroProto protocol
 *
 * Handles:
 * - Client connections with HELLO handshake
 * - Schema and property value sync on connect
 * - Property update broadcasts (rate-limited to ~15 Hz)
 * - Incoming property updates from clients
 */
class MicroProtoServer : public MessageHandler, public FlushListener {
public:
    static constexpr size_t TX_BUFFER_SIZE = MICROPROTO_TX_BUFFER_SIZE;
    static constexpr size_t MAX_CLIENTS = MICROPROTO_MAX_CLIENTS;
    static constexpr uint32_t BROADCAST_INTERVAL_MS = MICROPROTO_BROADCAST_INTERVAL_MS;

    MicroProtoServer(uint16_t port = 81)
        : _ws(port), _router(this), _nextSessionId(1) {}

    void begin();
    void loop();
    uint8_t connectedClients();
    void broadcastProperty(const PropertyBase* prop);
    void broadcastAllProperties();

    // MessageHandler interface (MVP)
    void onHello(uint8_t clientId, const Hello& hello) override;
    void onPropertyUpdate(uint8_t clientId, uint16_t propertyId, const void* value, size_t size) override;
    void onError(uint8_t clientId, const ErrorMessage& error) override;
    void onPing(uint8_t clientId, bool isResponse, uint32_t payload) override;
    void onConstraintViolation(uint8_t clientId, uint16_t propertyId, ErrorCode code) override;

    // Resource handlers
    void onResourceGetRequest(uint8_t clientId, uint8_t requestId, uint16_t propertyId, uint32_t resourceId) override;
    void onResourcePutRequest(uint8_t clientId, uint8_t requestId, uint16_t propertyId,
                               uint32_t resourceId,
                               const uint8_t* headerData, size_t headerLen,
                               const uint8_t* bodyData, size_t bodyLen) override;
    void onResourceDeleteRequest(uint8_t clientId, uint8_t requestId, uint16_t propertyId, uint32_t resourceId) override;

private:
    WebSocketsServer _ws;
    MessageRouter _router;
    uint32_t _nextSessionId;
    std::array<bool, MAX_CLIENTS> _clientReady = {};
    DirtySet _pendingBroadcast;
    uint32_t _lastBroadcastTime = 0;
    uint8_t _txBuf[TX_BUFFER_SIZE];   // Shared TX buffer (avoids 4KB stack alloc per method)
    uint8_t _auxBuf[TX_BUFFER_SIZE];  // Auxiliary buffer for resource body reads

    // FlushListener interface
    void onPropertiesChanged(const DirtySet& dirty) override;

    void handleEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
    void sendHelloResponse(uint8_t clientNum);
    void sendSchema(uint8_t clientNum);
    void sendAllPropertyValues(uint8_t clientNum);
    void sendError(uint8_t clientNum, const ErrorMessage& error);
    void sendPong(uint8_t clientNum, uint32_t payload);
    void broadcastPropertyExcept(const PropertyBase* prop, uint8_t excludeClient);
    void flushBroadcasts();

    // Resource response helpers
    void sendResourceGetOk(uint8_t clientNum, uint8_t requestId, const uint8_t* data, size_t len);
    void sendResourceGetError(uint8_t clientNum, uint8_t requestId, uint8_t errorCode);
    void sendResourcePutOk(uint8_t clientNum, uint8_t requestId, uint32_t resourceId);
    void sendResourcePutError(uint8_t clientNum, uint8_t requestId, uint8_t errorCode);
    void sendResourceDeleteOk(uint8_t clientNum, uint8_t requestId);
    void sendResourceDeleteError(uint8_t clientNum, uint8_t requestId, uint8_t errorCode);
};

} // namespace MicroProto

#endif // MICROPROTO_TRANSPORT_SERVER_H
