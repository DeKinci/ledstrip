#ifndef MICROPROTO_TRANSPORT_H
#define MICROPROTO_TRANSPORT_H

#include <stdint.h>
#include <stddef.h>

namespace MicroProto {

/**
 * Abstract transport interface for MicroProto.
 *
 * Transports handle the physical delivery of messages (WebSocket, BLE, etc.).
 * The MicroProtoController handles all protocol logic and routes messages
 * through registered transports.
 *
 * Each transport manages a range of client IDs (local to the transport).
 * The controller maps global client IDs to (transport, localClientId) pairs.
 */
class MicroProtoTransport {
public:
    virtual ~MicroProtoTransport() = default;

    /**
     * Send binary data to a client.
     * @param localClientId Client ID local to this transport (0-based)
     * @param data Binary MicroProto message
     * @param len Message length
     */
    virtual void send(uint8_t localClientId, const uint8_t* data, size_t len) = 0;

    /** Maximum number of concurrent clients this transport supports */
    virtual uint8_t maxClients() const = 0;

    /** Check if a specific client slot is connected */
    virtual bool isClientConnected(uint8_t localClientId) const = 0;

    /** Maximum packet size for a client (used in HELLO response) */
    virtual uint32_t maxPacketSize(uint8_t localClientId) const = 0;

    /** Transport capabilities */
    struct Capabilities {
        bool requiresBleExposed = false;  // Only sync ble_exposed properties
    };

    virtual Capabilities capabilities() const { return {}; }
};

} // namespace MicroProto

#endif // MICROPROTO_TRANSPORT_H
