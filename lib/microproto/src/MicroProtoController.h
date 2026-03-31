#ifndef MICROPROTO_CONTROLLER_H
#define MICROPROTO_CONTROLLER_H

#include "PropertyBase.h"
#include "PropertySystem.h"
#include "FunctionBase.h"
#include "messages/MessageRouter.h"
#include "messages/Hello.h"
#include "messages/Error.h"
#include "transport/MicroProtoTransport.h"
#include "wire/Buffer.h"
#include <array>

#ifndef MICROPROTO_TX_BUFFER_SIZE
#define MICROPROTO_TX_BUFFER_SIZE 4096
#endif

#ifndef MICROPROTO_BROADCAST_INTERVAL_MS
#define MICROPROTO_BROADCAST_INTERVAL_MS 67  // ~15 Hz
#endif

namespace MicroProto {

/**
 * MicroProtoController — the shared protocol engine.
 *
 * Handles all MicroProto protocol logic: HELLO handshake, property updates,
 * resource operations, RPC, broadcasts, schema sync. Transport-agnostic —
 * messages are delivered through registered MicroProtoTransport instances.
 *
 * Usage:
 *   MicroProtoController controller;
 *   WebSocketTransport ws(81);
 *   BleTransport ble;
 *
 *   controller.begin();
 *   controller.registerTransport(&ws);
 *   controller.registerTransport(&ble);
 *   ws.begin(&controller);
 *   ble.begin(&controller);
 *
 *   void loop() {
 *       ws.loop();
 *       ble.loop();
 *       controller.flushBroadcasts();
 *   }
 */
class MicroProtoController : public MessageHandler, public FlushListener {
public:
    static constexpr size_t TX_BUFFER_SIZE = MICROPROTO_TX_BUFFER_SIZE;
    static constexpr uint8_t MAX_TOTAL_CLIENTS = 12;
    static constexpr uint8_t MAX_TRANSPORTS = 4;
    static constexpr uint32_t BROADCAST_INTERVAL_MS = MICROPROTO_BROADCAST_INTERVAL_MS;

    MicroProtoController() : _router(this) {}

    /** Register as FlushListener with PropertySystem */
    void begin();

    /** Flush pending property broadcasts to all ready clients. Call from loop(). */
    void flushBroadcasts();

    /**
     * Register a transport. Returns the clientId offset for this transport.
     * Transport's local client IDs 0..maxClients()-1 map to global IDs
     * offset..offset+maxClients()-1.
     */
    uint8_t registerTransport(MicroProtoTransport* transport);

    /** Called by transports when binary data arrives from a client */
    void processMessage(uint8_t globalClientId, const uint8_t* data, size_t len);

    /** Called by transports on client connect/disconnect */
    void onClientConnected(uint8_t globalClientId);
    void onClientDisconnected(uint8_t globalClientId);

    /** Total connected clients across all transports */
    uint8_t connectedClients() const;

    // =========== MessageHandler interface ===========

    void onHello(uint8_t clientId, const Hello& hello) override;
    void onPropertyUpdate(uint8_t clientId, uint16_t propertyId,
                          const void* value, size_t size) override;
    void onError(uint8_t clientId, const ErrorMessage& error) override;
    void onPing(uint8_t clientId, bool isResponse, uint32_t payload) override;
    void onRpcRequest(uint8_t clientId, uint16_t functionId, uint8_t callId,
                      bool needsResponse, ReadBuffer& params) override;
    void onRpcResponse(uint8_t clientId, uint8_t callId, bool success,
                       ReadBuffer& result) override;
    void onRpcError(uint8_t clientId, uint8_t callId, uint8_t errorCode,
                    const char* message, size_t messageLen) override;
    void onConstraintViolation(uint8_t clientId, uint16_t propertyId,
                               ErrorCode code) override;
    void onResourceGetRequest(uint8_t clientId, uint8_t requestId,
                              uint16_t propertyId, uint32_t resourceId) override;
    void onResourcePutRequest(uint8_t clientId, uint8_t requestId,
                              uint16_t propertyId, uint32_t resourceId,
                              const uint8_t* headerData, size_t headerLen,
                              const uint8_t* bodyData, size_t bodyLen) override;
    void onResourceDeleteRequest(uint8_t clientId, uint8_t requestId,
                                 uint16_t propertyId, uint32_t resourceId) override;

    // =========== FlushListener ===========

    void onPropertiesChanged(const DirtySet& dirty) override;

private:
    MessageRouter _router;
    uint32_t _nextSessionId = 1;

    struct TransportSlot {
        MicroProtoTransport* transport = nullptr;
        uint8_t clientIdOffset = 0;
        uint8_t clientCount = 0;
    };
    std::array<TransportSlot, MAX_TRANSPORTS> _transports = {};
    uint8_t _transportCount = 0;

    std::array<bool, MAX_TOTAL_CLIENTS> _clientReady = {};
    DirtySet _pendingBroadcast;
    uint32_t _lastBroadcastTime = 0;

    uint8_t _txBuf[TX_BUFFER_SIZE];
    uint8_t _auxBuf[TX_BUFFER_SIZE];

    // Routing
    TransportSlot* findTransport(uint8_t globalClientId);
    void sendToClient(uint8_t globalClientId, const uint8_t* data, size_t len);
    bool requiresBleExposed(uint8_t globalClientId);

    // Protocol helpers
    void sendHelloResponse(uint8_t clientId);
    void sendSchema(uint8_t clientId);
    void sendAllPropertyValues(uint8_t clientId);
    void sendError(uint8_t clientId, const ErrorMessage& error);
    void sendPong(uint8_t clientId, uint32_t payload);
    void sendRpcResponse(uint8_t clientId, uint8_t callId,
                         const uint8_t* data, size_t len);
    void sendRpcError(uint8_t clientId, uint8_t callId,
                      uint8_t errorCode, const char* message);
    void broadcastPropertyExcept(const PropertyBase* prop, uint8_t excludeClient);

    // Resource response helpers
    void sendResourceGetOk(uint8_t clientId, uint8_t requestId,
                           const uint8_t* data, size_t len);
    void sendResourceGetError(uint8_t clientId, uint8_t requestId, uint8_t errorCode);
    void sendResourcePutOk(uint8_t clientId, uint8_t requestId, uint32_t resourceId);
    void sendResourcePutError(uint8_t clientId, uint8_t requestId, uint8_t errorCode);
    void sendResourceDeleteOk(uint8_t clientId, uint8_t requestId);
    void sendResourceDeleteError(uint8_t clientId, uint8_t requestId, uint8_t errorCode);
};

} // namespace MicroProto

#endif // MICROPROTO_CONTROLLER_H
