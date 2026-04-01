#pragma once
#include <stdint.h>
#include "MatterConst.h"
#include "MatterMessage.h"
#include "MatterClusters.h"

namespace matter {

class MatterTransport;
class MatterSession;

// ---------------------------------------------------------------------------
// Subscription state
// Limitation: Only a single active subscription is supported.
// A new SubscribeRequest silently replaces the previous subscription.
// ---------------------------------------------------------------------------
struct Subscription {
    bool     active = false;
    uint32_t id = 0;
    uint16_t minIntervalSec = 0;
    uint16_t maxIntervalSec = 60;
    uint32_t lastReportMs = 0;

    static constexpr size_t kMaxPaths = 16;
    struct Path {
        uint16_t endpoint;
        uint32_t cluster;
        uint32_t attribute;
        bool     active = false;
    } paths[kMaxPaths];
    size_t pathCount = 0;
};

// ---------------------------------------------------------------------------
// MatterIM — Interaction Model handler
// ---------------------------------------------------------------------------
class MatterIM {
public:
    void setClusters(ClusterBinding* clusters) { _clusters = clusters; }
    void setSession(MatterSession* session) { _session = session; }

    // Handle an IM-protocol message. Returns true if handled.
    bool handleIM(uint8_t opcode,
                  const uint8_t* payload, size_t payloadLen,
                  const MessageHeader& msgHdr,
                  const ProtocolHeader& protoHdr,
                  MatterTransport& transport);

    // Check if subscription report is due; send if needed
    void checkSubscription(MatterTransport& transport);

    Subscription& subscription() { return _sub; }

private:
    void handleReadRequest(const uint8_t* payload, size_t len,
                           const MessageHeader& msgHdr,
                           const ProtocolHeader& protoHdr,
                           MatterTransport& transport);

    void handleWriteRequest(const uint8_t* payload, size_t len,
                            const MessageHeader& msgHdr,
                            const ProtocolHeader& protoHdr,
                            MatterTransport& transport);

    void handleInvokeRequest(const uint8_t* payload, size_t len,
                             const MessageHeader& msgHdr,
                             const ProtocolHeader& protoHdr,
                             MatterTransport& transport);

    void handleSubscribeRequest(const uint8_t* payload, size_t len,
                                const MessageHeader& msgHdr,
                                const ProtocolHeader& protoHdr,
                                MatterTransport& transport);

    void handleStatusResponse(const uint8_t* payload, size_t len,
                              const MessageHeader& msgHdr,
                              const ProtocolHeader& protoHdr,
                              MatterTransport& transport);

    // Commissioning cluster command dispatch
    bool handleCommissioningCommand(uint16_t endpoint, uint32_t cluster,
                                     uint32_t command,
                                     TLVReader& rd,
                                     TLVWriter& respWriter,
                                     MatterTransport& transport);

    ClusterBinding* _clusters = nullptr;
    MatterSession*  _session  = nullptr;
    Subscription    _sub;
    uint32_t        _subIdCounter = 1;

    // Pending SubscribeResponse state (deferred until StatusResponse)
    struct {
        bool     pending = false;
        uint32_t subId = 0;
        uint16_t maxInterval = 0;
        uint16_t exchangeId = 0;
        uint32_t ackCounter = 0;
    } _pendingSub;
};

} // namespace matter
