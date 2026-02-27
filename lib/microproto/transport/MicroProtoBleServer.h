#ifndef MICROPROTO_TRANSPORT_BLE_SERVER_H
#define MICROPROTO_TRANSPORT_BLE_SERVER_H

#include <NimBLEDevice.h>
#include <array>
#include <atomic>
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

class MicroProtoBleServer : public MessageHandler,
                            public FlushListener,
                            public NimBLEServerCallbacks,
                            public NimBLECharacteristicCallbacks {
public:
    static constexpr size_t TX_BUFFER_SIZE = 512;
    static constexpr size_t MAX_CLIENTS = 3;
    static constexpr uint32_t BROADCAST_INTERVAL_MS = 67;  // ~15 Hz
    static constexpr size_t RX_QUEUE_SIZE = 4;

    MicroProtoBleServer() : _router(this), _nextSessionId(1) {}

    void begin();
    void loop();
    uint8_t connectedClients();

    // MessageHandler interface
    void onHello(uint8_t clientId, const Hello& hello) override;
    void onPropertyUpdate(uint8_t clientId, uint16_t propertyId, const void* value, size_t size) override;
    void onError(uint8_t clientId, const ErrorMessage& error) override;
    void onPing(uint8_t clientId, bool isResponse, uint32_t payload) override;
    void onConstraintViolation(uint8_t clientId, uint16_t propertyId, ErrorCode code) override;

    // NimBLEServerCallbacks
    void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override;
    void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override;
    void onMTUChange(uint16_t mtu, NimBLEConnInfo& connInfo) override;

    // NimBLECharacteristicCallbacks
    void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo) override;

private:
    MessageRouter _router;
    uint32_t _nextSessionId;

    NimBLEServer* _server = nullptr;
    NimBLECharacteristic* _txChar = nullptr;
    NimBLECharacteristic* _rxChar = nullptr;

    struct BleClient {
        uint16_t connHandle = 0;
        uint16_t mtu = 23;  // BLE default MTU
        bool ready = false;
        bool valid = false;
    };
    std::array<BleClient, MAX_CLIENTS> _clients = {};

    struct QueuedMessage {
        uint8_t data[TX_BUFFER_SIZE];
        size_t length = 0;
        uint8_t clientId = 0;
        bool valid = false;
    };
    std::array<QueuedMessage, RX_QUEUE_SIZE> _rxQueue = {};
    std::atomic<uint8_t> _rxHead{0};
    std::atomic<uint8_t> _rxTail{0};

    DirtySet _pendingBroadcast;
    uint32_t _lastBroadcastTime = 0;

    uint8_t findClientSlot(uint16_t connHandle);
    uint8_t allocClientSlot(uint16_t connHandle);
    void freeClientSlot(uint16_t connHandle);

    uint16_t maxPayload(uint8_t clientIdx) const;
    void sendToClient(uint8_t clientIdx, const uint8_t* data, size_t length);
    void sendHelloResponse(uint8_t clientIdx);
    void sendSchema(uint8_t clientIdx);
    void sendAllPropertyValues(uint8_t clientIdx);
    void sendError(uint8_t clientIdx, const ErrorMessage& error);
    void sendPong(uint8_t clientIdx, uint32_t payload);
    void broadcastPropertyExcept(const PropertyBase* prop, uint8_t excludeClient);

    void processRxQueue();
    void onPropertiesChanged(const DirtySet& dirty) override;
    void flushBroadcasts();
    void startAdvertising();
    bool _advertisingConfigured = false;
};

} // namespace MicroProto

#endif // MICROPROTO_TRANSPORT_BLE_SERVER_H
