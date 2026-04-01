#pragma once
#include <stdint.h>
#include "MatterConst.h"
#include "MatterMessage.h"
#include "MatterSession.h"
#include "MatterIM.h"
#include "MatterMRP.h"
#include "MatterClusters.h"

#ifndef NATIVE_TEST
#include <WiFiUdp.h>
#include <ESPmDNS.h>
#endif

namespace matter {

// ---------------------------------------------------------------------------
// MatterRuntime — all runtime state, allocated on begin()
//
// When Matter is not started, MatterTransport uses near-zero RAM.
// A single allocation (~9 KB) happens on begin() and is never freed.
// ---------------------------------------------------------------------------
struct MatterRuntime {
    MatterSession  session;
    MatterIM       im;
    MRP            mrp;

    uint8_t  rxBuf[kMaxMessageSize] = {};
    uint8_t  txBuf[kMaxMessageSize] = {};
    uint32_t peerIp   = 0;
    uint16_t peerPort = 0;
    uint16_t nextExchangeId = 0x100;
    bool     operationalMdns = false;
};

// ---------------------------------------------------------------------------
// MatterTransport — top-level Matter-over-WiFi integration
//
// Lightweight shell (~32 bytes) until begin() is called.
// Runtime state (~9 KB) is heap-allocated once on begin().
//
// Usage in main.cpp:
//
//   MatterTransport matter;
//
//   void setup() {
//       matter.mapOnOff(propOn);
//       matter.mapLevel(propBrightness);
//       matter.mapColor(propColor);
//       matter.begin();
//   }
//   void loop() {
//       matter.loop();
//   }
// ---------------------------------------------------------------------------
class MatterTransport {
public:
    // --- Property mapping (call before begin) ---
    void mapOnOff(MicroProto::Property<bool>& prop)            { _clusters.onOff = &prop; }
    void mapLevel(MicroProto::Property<uint8_t>& prop)         { _clusters.brightness = &prop; }
    void mapColor(MicroProto::ArrayProperty<uint8_t, 3>& prop) { _clusters.color = &prop; }

    void setDeviceName(const char* name) { _clusters.productName = name; }
    void setVendorName(const char* name) { _clusters.vendorName = name; }

    // --- Lifecycle ---
    bool begin(uint32_t passcode = kTestPasscode,
               uint16_t discriminator = kTestDiscriminator);
    void loop();

    // --- Called by Session / IM to send messages ---
    void sendSecureChannel(uint8_t opcode, const uint8_t* payload, size_t len,
                           uint16_t exchangeId, uint32_t ackCounter);
    void sendIM(uint8_t opcode, const uint8_t* payload, size_t len,
                uint16_t exchangeId, uint32_t ackCounter,
                bool initiator = false);

    // Generate a new exchange ID for device-initiated exchanges
    uint16_t nextExchangeId() { return _rt ? _rt->nextExchangeId++ : 0; }

    bool isCommissioned() const { return _rt && _rt->session.isCommissioned(); }

    // Debug access (for test firmware)
    MatterSession* session() { return _rt ? &_rt->session : nullptr; }
    const MatterSession* session() const { return _rt ? &_rt->session : nullptr; }

    // Reset commissioning — clears session, MRP, and peer state
    void resetCommissioning() {
        if (!_rt) return;
        _rt->session.openCommissioning();
        _rt->mrp.reset();
        _rt->peerIp = 0;
        _rt->peerPort = 0;
    }

private:
    void handlePacket(const uint8_t* data, size_t len,
                      uint32_t srcIp, uint16_t srcPort);
    void sendRaw(uint16_t protocolId, uint8_t opcode,
                 const uint8_t* payload, size_t payloadLen,
                 uint16_t exchangeId, uint32_t ackCounter,
                 bool encrypted, bool initiator = false);
    void advertiseMdns();
    void advertiseOperational();

    // ClusterBinding is tiny (~80 bytes) and set before begin(), so it stays inline
    ClusterBinding _clusters;
    MatterRuntime* _rt = nullptr;  // Allocated on begin()
    bool           _started = false;

#ifndef NATIVE_TEST
    WiFiUDP _udp;
#endif
};

} // namespace matter
