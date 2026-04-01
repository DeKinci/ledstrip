#include "MatterTransport.h"
#include "MatterTLV.h"
#include <string.h>

#ifdef NATIVE_TEST
namespace matter {
bool MatterTransport::begin(uint32_t, uint16_t) { return false; }
void MatterTransport::loop() {}
void MatterTransport::sendSecureChannel(uint8_t, const uint8_t*, size_t, uint16_t, uint32_t) {}
void MatterTransport::sendIM(uint8_t, const uint8_t*, size_t, uint16_t, uint32_t, bool) {}
uint32_t MRP::millis() { return 0; }
} // namespace matter

#else

#include <WiFi.h>

namespace matter {

// ---------------------------------------------------------------------------
// MRP millis wrapper (declared in MatterMRP.h)
// ---------------------------------------------------------------------------
uint32_t MRP::millis() { return ::millis(); }

// ---------------------------------------------------------------------------
// begin / loop
// ---------------------------------------------------------------------------

bool MatterTransport::begin(uint32_t passcode, uint16_t discriminator) {
    // Single heap allocation for all runtime state
    if (!_rt) {
        _rt = new (std::nothrow) MatterRuntime();
        if (!_rt) return false;
    }

    _rt->session.begin(passcode, discriminator);
    _rt->im.setClusters(&_clusters);
    _rt->im.setSession(&_rt->session);

    // Initialize MRP send counter to a random value
    uint8_t rndBuf[4];
    randomBytes(rndBuf, 4);
    uint32_t rndVal = rndBuf[0] | ((uint32_t)rndBuf[1] << 8) |
                      ((uint32_t)rndBuf[2] << 16) | ((uint32_t)rndBuf[3] << 24);
    _rt->mrp.initCounter(rndVal);

    if (!_udp.begin(kPort)) return false;

    // Set commissioned flag from persisted fabric state
    _clusters.commissioned = _rt->session.isCommissioned();

    advertiseMdns();
    _started = true;
    return true;
}

void MatterTransport::loop() {
    if (!_started || !_rt) return;

    // Receive
    int pktSize = _udp.parsePacket();
    if (pktSize > 0 && (size_t)pktSize <= sizeof(_rt->rxBuf)) {
        size_t len = _udp.read(_rt->rxBuf, sizeof(_rt->rxBuf));
        uint32_t srcIp = (uint32_t)_udp.remoteIP();
        uint16_t srcPort = _udp.remotePort();
        handlePacket(_rt->rxBuf, len, srcIp, srcPort);
    }

    // MRP retransmit
    size_t retLen;
    const uint8_t* retData = _rt->mrp.tick(retLen);
    if (retData && _rt->peerIp) {
        IPAddress ip(_rt->peerIp);
        _udp.beginPacket(ip, _rt->peerPort);
        _udp.write(retData, retLen);
        _udp.endPacket();
    }

    // Subscription reports
    _rt->im.checkSubscription(*this);

    // Switch to operational mDNS after commissioning
    if (_rt->session.isCommissioned() && !_rt->operationalMdns) {
        advertiseOperational();
        _rt->operationalMdns = true;
    }
}

// ---------------------------------------------------------------------------
// Packet handling
// ---------------------------------------------------------------------------

void MatterTransport::handlePacket(const uint8_t* data, size_t len,
                                    uint32_t srcIp, uint16_t srcPort) {
    _rt->peerIp = srcIp;
    _rt->peerPort = srcPort;

    MessageHeader hdr;
    size_t hdrLen = hdr.decode(data, len);
    if (hdrLen == 0 || hdrLen >= len) return;

    const uint8_t* afterHdr = data + hdrLen;
    size_t afterHdrLen = len - hdrLen;

    if (hdr.sessionId == kUnsecuredSessionId) {
        // Unsecured — PASE/CASE initiation
        ProtocolHeader proto;
        size_t protoLen = proto.decode(afterHdr, afterHdrLen);
        if (protoLen == 0) return;

        if (_rt->mrp.isDuplicate(hdr.messageCounter)) return;
        _rt->mrp.onReceived(hdr.messageCounter, proto.needsAck);
        if (proto.hasAck) _rt->mrp.onAckReceived(proto.ackCounter);

        const uint8_t* payload = afterHdr + protoLen;
        size_t payloadLen = afterHdrLen - protoLen;

        if (proto.protocolId == kProtoSecureChannel) {
            _rt->session.handleSecureChannel(proto.opcode, payload, payloadLen,
                                              hdr, proto, *this);
        }
    } else if (_rt->session.isSecure() && hdr.sessionId == _rt->session.localSessionId()) {
        // Secured session — decrypt then process
        uint8_t plain[kMaxMessageSize];
        size_t plainLen = _rt->session.decrypt(hdr, afterHdr, afterHdrLen, plain);
        if (plainLen == 0) return;

        ProtocolHeader proto;
        size_t protoLen = proto.decode(plain, plainLen);
        if (protoLen == 0) return;

        if (_rt->mrp.isDuplicate(hdr.messageCounter)) return;
        _rt->mrp.onReceived(hdr.messageCounter, proto.needsAck);
        if (proto.hasAck) _rt->mrp.onAckReceived(proto.ackCounter);

        const uint8_t* payload = plain + protoLen;
        size_t payloadLen = plainLen - protoLen;

        if (proto.protocolId == kProtoInteractionModel) {
            _rt->im.handleIM(proto.opcode, payload, payloadLen, hdr, proto, *this);
        } else if (proto.protocolId == kProtoSecureChannel) {
            _rt->session.handleSecureChannel(proto.opcode, payload, payloadLen,
                                              hdr, proto, *this);
        }
    }
}

// ---------------------------------------------------------------------------
// Message sending
// ---------------------------------------------------------------------------

void MatterTransport::sendSecureChannel(uint8_t opcode,
                                         const uint8_t* payload, size_t len,
                                         uint16_t exchangeId, uint32_t ackCounter) {
    sendRaw(kProtoSecureChannel, opcode, payload, len, exchangeId, ackCounter, false);
}

void MatterTransport::sendIM(uint8_t opcode,
                              const uint8_t* payload, size_t len,
                              uint16_t exchangeId, uint32_t ackCounter,
                              bool initiator) {
    sendRaw(kProtoInteractionModel, opcode, payload, len, exchangeId, ackCounter, true, initiator);
}

void MatterTransport::sendRaw(uint16_t protocolId, uint8_t opcode,
                               const uint8_t* payload, size_t payloadLen,
                               uint16_t exchangeId, uint32_t ackCounter,
                               bool encrypted, bool initiator) {
    if (!_rt) return;
    size_t pos = 0;

    // Message header
    MessageHeader hdr;
    hdr.messageCounter = _rt->mrp.nextSendCounter();
    hdr.securityFlags = 0;
    if (encrypted && _rt->session.isSecure()) {
        hdr.sessionId = _rt->session.peerSessionId();
    } else {
        hdr.sessionId = kUnsecuredSessionId;
    }
    pos += hdr.encode(_rt->txBuf, sizeof(_rt->txBuf));
    size_t hdrEnd = pos;

    // Protocol header
    ProtocolHeader proto;
    proto.opcode = opcode;
    proto.exchangeId = exchangeId;
    proto.protocolId = protocolId;
    proto.isInitiator = initiator;
    proto.needsAck = true;
    if (ackCounter != 0 || _rt->mrp.hasPendingAck()) {
        proto.hasAck = true;
        proto.ackCounter = _rt->mrp.hasPendingAck() ? _rt->mrp.pendingAckCounter() : ackCounter;
        _rt->mrp.clearPendingAck();
    }

    if (encrypted && _rt->session.isSecure()) {
        // Build plaintext: proto header + payload
        uint8_t plain[kMaxMessageSize];
        size_t plainLen = proto.encode(plain, sizeof(plain));
        memcpy(plain + plainLen, payload, payloadLen);
        plainLen += payloadLen;

        // Encrypt: AAD = message header bytes
        size_t encLen = _rt->session.encrypt(_rt->txBuf, hdrEnd,
                                              plain, plainLen,
                                              hdr.messageCounter,
                                              _rt->txBuf + pos);
        pos += encLen;
    } else {
        pos += proto.encode(_rt->txBuf + pos, sizeof(_rt->txBuf) - pos);
        memcpy(_rt->txBuf + pos, payload, payloadLen);
        pos += payloadLen;
    }

    // Send
    if (_rt->peerIp && pos > 0) {
        IPAddress ip(_rt->peerIp);
        _udp.beginPacket(ip, _rt->peerPort);
        _udp.write(_rt->txBuf, pos);
        _udp.endPacket();

        // Track for MRP retransmission
        _rt->mrp.trackSent(_rt->txBuf, pos, hdr.messageCounter);
    }
}

// ---------------------------------------------------------------------------
// mDNS advertisement
// ---------------------------------------------------------------------------

void MatterTransport::advertiseMdns() {
    // Use MAC-derived hostname for unique commissionable instance name (Spec 4.3.1.1)
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char hostname[18];
    snprintf(hostname, sizeof(hostname), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    if (!MDNS.begin(hostname)) return;

    MDNS.addService("_matterc", "_udp", kPort);

    char disc[8];
    snprintf(disc, sizeof(disc), "%u", kTestDiscriminator);
    MDNS.addServiceTxt("_matterc", "_udp", "D", (const char*)disc);

    char vp[16];
    snprintf(vp, sizeof(vp), "%u+%u", kTestVendorId, kTestProductId);
    MDNS.addServiceTxt("_matterc", "_udp", "VP", (const char*)vp);

    MDNS.addServiceTxt("_matterc", "_udp", "CM", "1");
    MDNS.addServiceTxt("_matterc", "_udp", "DT", "269");
    MDNS.addServiceTxt("_matterc", "_udp", "DN", _clusters.productName);
    MDNS.addServiceTxt("_matterc", "_udp", "SII", "5000");
    MDNS.addServiceTxt("_matterc", "_udp", "SAI", "300");
    MDNS.addServiceTxt("_matterc", "_udp", "PH", "4");  // Spec 5.4.2.5.6: IP pairing
    MDNS.addServiceTxt("_matterc", "_udp", "PI", "");
}

void MatterTransport::advertiseOperational() {
    if (!_rt) return;

    // Compute compressed fabric ID (Spec 4.3.2.2)
    // Load root cert → extract pub key → HKDF to get compressed ID
    uint8_t rootCert[kMaxCertSize];
    size_t rootLen = FabricStore::loadCert("root", rootCert, sizeof(rootCert));
    uint8_t rootPubKey[kP256PubKeySize];

    uint8_t cfid[8] = {};
    if (rootLen > 0 && extractPubKeyFromCert(rootCert, rootLen, rootPubKey)) {
        compressedFabricId(rootPubKey, _rt->session.fabric().fabricId, cfid);
    }

    // Instance name: <CompressedFabricId>-<NodeId> (Spec 4.3.1)
    char instance[34];
    snprintf(instance, sizeof(instance),
             "%02X%02X%02X%02X%02X%02X%02X%02X-%08X%08X",
             cfid[0], cfid[1], cfid[2], cfid[3],
             cfid[4], cfid[5], cfid[6], cfid[7],
             (uint32_t)(_rt->session.nodeId() >> 32),
             (uint32_t)(_rt->session.nodeId() & 0xFFFFFFFF));

    // Restart mDNS with operational instance name
    MDNS.end();
    MDNS.begin(instance);

    MDNS.addService("_matter", "_tcp", kPort);
    MDNS.addServiceTxt("_matter", "_tcp", "SII", "5000");
    MDNS.addServiceTxt("_matter", "_tcp", "SAI", "300");
    MDNS.addServiceTxt("_matter", "_tcp", "T", "0");
}

} // namespace matter
#endif // NATIVE_TEST
