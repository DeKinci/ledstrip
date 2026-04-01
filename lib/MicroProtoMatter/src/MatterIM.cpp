#include "MatterIM.h"
#include "MatterTLV.h"
#include "MatterSession.h"
#include "MatterTransport.h"
#include "MatterCrypto.h"
#include "test_certs.h"
#include <string.h>

#ifdef NATIVE_TEST
static uint32_t millis() { return 0; }
#else
#include <Arduino.h>
#endif

namespace matter {

// InteractionModelRevision (tag 0xFF) — can't use putU8(0xFF, ...) because
// 0xFF collides with the kAnon sentinel in TLVWriter, so emit raw bytes.
static void writeIMRevision(TLVWriter& wr) {
    static const uint8_t rev[] = {kTagContext | kTLVUInt8, 0xFF, kInteractionModelRevision};
    wr.putRaw(rev, sizeof(rev));
}

// ---------------------------------------------------------------------------
// IM dispatch
// ---------------------------------------------------------------------------

bool MatterIM::handleIM(uint8_t opcode,
                         const uint8_t* payload, size_t payloadLen,
                         const MessageHeader& msgHdr,
                         const ProtocolHeader& protoHdr,
                         MatterTransport& transport) {
    switch (opcode) {
        case kOpReadRequest:
            handleReadRequest(payload, payloadLen, msgHdr, protoHdr, transport);
            return true;
        case kOpWriteRequest:
            handleWriteRequest(payload, payloadLen, msgHdr, protoHdr, transport);
            return true;
        case kOpInvokeRequest:
            handleInvokeRequest(payload, payloadLen, msgHdr, protoHdr, transport);
            return true;
        case kOpSubscribeRequest:
            handleSubscribeRequest(payload, payloadLen, msgHdr, protoHdr, transport);
            return true;
        case kOpStatusResponse:
            handleStatusResponse(payload, payloadLen, msgHdr, protoHdr, transport);
            return true;
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// ReadRequest → ReportData
//
// ReadRequestMessage {
//   attributeRequests [0]: [AttributePathIB]
//   fabricFiltered    [3]: BOOL
// }
// AttributePathIB (list) { endpoint [2], cluster [3], attribute [4] }
// ---------------------------------------------------------------------------

void MatterIM::handleReadRequest(const uint8_t* payload, size_t len,
                                  const MessageHeader& msgHdr,
                                  const ProtocolHeader& protoHdr,
                                  MatterTransport& transport) {
    uint8_t resp[1024];
    TLVWriter wr(resp, sizeof(resp));
    wr.openStruct();
    wr.openArray(1);  // tag 1: AttributeReports

    TLVReader rd(payload, len);
    if (rd.next() && rd.isStruct()) {
        while (rd.next() && !rd.isEnd()) {
            if (rd.tag() == 0 && rd.isArray()) {
                // tag 0: attributeRequests array
                while (rd.next() && !rd.isEnd()) {
                    if (rd.isList()) {
                        uint16_t ep = 0xFFFF;
                        uint32_t cl = 0, at = 0;
                        while (rd.next() && !rd.isEnd()) {
                            if (rd.tag() == 2) ep = rd.getU16();
                            if (rd.tag() == 3) cl = rd.getU32();
                            if (rd.tag() == 4) at = rd.getU32();
                        }
                        if (_clusters) {
                            // Wildcard: iterate all endpoints; specific: just the one
                            static const uint16_t kEndpoints[] = {0, 1};
                            for (uint16_t eid : kEndpoints) {
                                if (ep != 0xFFFF && eid != ep) continue;

                                uint8_t tmpBuf[256];
                                TLVWriter tmpWr(tmpBuf, sizeof(tmpBuf));
                                bool ok = _clusters->readAttribute(eid, cl, at, tmpWr, 2);

                                wr.openStruct(kAnon);   // AttributeReportIB
                                if (ok && !tmpWr.error()) {
                                    wr.openStruct(1);        // tag 1: AttributeDataIB
                                    wr.putU32(0, _clusters->dataVersion);
                                    wr.openList(1);          // tag 1: AttributePath
                                    wr.putU16(2, eid);
                                    wr.putU32(3, cl);
                                    wr.putU32(4, at);
                                    wr.closeContainer();
                                    wr.putRaw(tmpBuf, tmpWr.size()); // tag 2: Data
                                    wr.closeContainer();     // /AttributeDataIB
                                } else {
                                    // For wildcard: skip unsupported, don't error
                                    if (ep == 0xFFFF) { wr.closeContainer(); continue; }
                                    // Spec 8.4.3.2: AttributeStatusIB for unsupported attributes
                                    wr.openStruct(0);        // tag 0: AttributeStatusIB
                                    wr.openList(0);          // tag 0: AttributePath
                                    wr.putU16(2, eid);
                                    wr.putU32(3, cl);
                                    wr.putU32(4, at);
                                    wr.closeContainer();
                                    wr.openStruct(1);        // tag 1: StatusIB
                                    wr.putU8(0, kStatusUnsupportedAttribute);
                                    wr.closeContainer();
                                    wr.closeContainer();     // /AttributeStatusIB
                                }
                                wr.closeContainer();     // /AttributeReportIB
                            }
                        }
                    }
                }
            } else {
                if (rd.isStruct() || rd.isArray() || rd.isList()) rd.skipContainer();
            }
        }
    }

    wr.closeContainer();      // /AttributeReports
    wr.putBool(4, false);    // tag 4: SuppressResponse
    writeIMRevision(wr);
    wr.closeContainer();      // /ReportData

    if (!wr.error())
        transport.sendIM(kOpReportData, resp, wr.size(),
                         protoHdr.exchangeId, msgHdr.messageCounter);
}

// ---------------------------------------------------------------------------
// WriteRequest → WriteResponse
//
// WriteRequestMessage {
//   suppressResponse [0]: BOOL
//   timedRequest     [1]: BOOL
//   writeRequests    [2]: [AttributeDataIB]
// }
// AttributeDataIB { dataVersion [0], path [1] (list), data [2] }
// ---------------------------------------------------------------------------

void MatterIM::handleWriteRequest(const uint8_t* payload, size_t len,
                                   const MessageHeader& msgHdr,
                                   const ProtocolHeader& protoHdr,
                                   MatterTransport& transport) {
    uint8_t resp[512];
    TLVWriter wr(resp, sizeof(resp));
    wr.openStruct();
    wr.openArray(0);  // tag 0: WriteResponses

    TLVReader rd(payload, len);
    if (rd.next() && rd.isStruct()) {
        while (rd.next() && !rd.isEnd()) {
            if (rd.tag() == 2 && rd.isArray()) {
                // tag 2: writeRequests array
                while (rd.next() && !rd.isEnd()) {
                    if (rd.isStruct()) {
                        uint16_t ep = 0xFFFF;
                        uint32_t cl = 0, at = 0;
                        bool found = false;
                        while (rd.next() && !rd.isEnd()) {
                            if (rd.tag() == 1 && rd.isList()) {
                                // tag 1: AttributePath
                                while (rd.next() && !rd.isEnd()) {
                                    if (rd.tag() == 2) ep = rd.getU16();
                                    if (rd.tag() == 3) cl = rd.getU32();
                                    if (rd.tag() == 4) at = rd.getU32();
                                }
                            }
                            if (rd.tag() == 2) {
                                // tag 2: Data value
                                if (ep != 0xFFFF && _clusters) {
                                    found = _clusters->writeAttribute(ep, cl, at, rd);
                                }
                            }
                        }
                        // Write AttributeStatusIB for this attribute
                        wr.openStruct(kAnon);
                        wr.openStruct(0);
                        wr.openList(0);    // AttributePath
                        wr.putU16(2, ep);
                        wr.putU32(3, cl);
                        wr.putU32(4, at);
                        wr.closeContainer();
                        wr.openStruct(1);  // StatusIB
                        wr.putU8(0, found ? kStatusSuccess : kStatusUnsupportedAttribute);
                        wr.closeContainer();
                        wr.closeContainer();
                        wr.closeContainer();
                    }
                }
            } else {
                if (rd.isStruct() || rd.isArray() || rd.isList()) rd.skipContainer();
            }
        }
    }

    wr.closeContainer();
    writeIMRevision(wr);
    wr.closeContainer();

    transport.sendIM(kOpWriteResponse, resp, wr.size(),
                     protoHdr.exchangeId, msgHdr.messageCounter);
}

// ---------------------------------------------------------------------------
// InvokeRequest → InvokeResponse
//
// InvokeRequestMessage {
//   suppressResponse [0]: BOOL
//   timedRequest     [1]: BOOL
//   invokeRequests   [2]: [CommandDataIB]
// }
// CommandDataIB { commandPath [0] (list), commandFields [1] (struct) }
// CommandPathIB (list) { endpoint [0], cluster [1], command [2] }
//
// InvokeResponseMessage {
//   suppressResponse [0]: BOOL
//   invokeResponses  [1]: [InvokeResponseIB]
// }
// InvokeResponseIB { command [0]: CommandDataIB  OR  status [1]: CommandStatusIB }
// ---------------------------------------------------------------------------

static void writeStatusEntry(TLVWriter& wr, uint16_t ep, uint32_t cl,
                              uint32_t cmd, uint8_t status) {
    wr.openStruct(kAnon);      // InvokeResponseIB
    wr.openStruct(1);          // tag 1: CommandStatusIB
    wr.openList(0);            // tag 0: CommandPath
    wr.putU16(0, ep);
    wr.putU32(1, cl);
    wr.putU32(2, cmd);
    wr.closeContainer();
    wr.openStruct(1);          // tag 1: StatusIB
    wr.putU8(0, status);
    wr.closeContainer();
    wr.closeContainer();       // /CommandStatusIB
    wr.closeContainer();       // /InvokeResponseIB
}

void MatterIM::handleInvokeRequest(const uint8_t* payload, size_t len,
                                    const MessageHeader& msgHdr,
                                    const ProtocolHeader& protoHdr,
                                    MatterTransport& transport) {
    uint8_t resp[1024];
    TLVWriter wr(resp, sizeof(resp));
    wr.openStruct();
    wr.putBool(0, false);  // tag 0: SuppressResponse
    wr.openArray(1);       // tag 1: InvokeResponses

    TLVReader rd(payload, len);
    if (rd.next() && rd.isStruct()) {
        while (rd.next() && !rd.isEnd()) {
            if (rd.tag() == 2 && rd.isArray()) {
                // tag 2: invokeRequests
                while (rd.next() && !rd.isEnd()) {
                    if (rd.isStruct()) {
                        // CommandDataIB
                        uint16_t ep = 0xFFFF;
                        uint32_t cl = 0, cmd = 0;
                        bool handled = false;

                        while (rd.next() && !rd.isEnd()) {
                            if (rd.tag() == 0 && rd.isList()) {
                                // tag 0: CommandPath
                                while (rd.next() && !rd.isEnd()) {
                                    if (rd.tag() == 0) ep = rd.getU16();
                                    if (rd.tag() == 1) cl = rd.getU32();
                                    if (rd.tag() == 2) cmd = rd.getU32();
                                }
                            }
                            if (rd.tag() == 1 && rd.isStruct()) {
                                // tag 1: CommandFields — pass reader to handlers
                                if (ep == 0 && _session) {
                                    handled = handleCommissioningCommand(
                                        ep, cl, cmd, rd, wr, transport);
                                }
                                if (!handled && ep != 0xFFFF && _clusters) {
                                    handled = _clusters->handleCommand(ep, cl, cmd, rd, wr);
                                }
                                if (!handled) rd.skipContainer();
                            }
                        }

                        // Write response if not already written by handler
                        if (!handled) {
                            writeStatusEntry(wr, ep, cl, cmd, kStatusUnsupportedCommand);
                        } else if (ep != 0) {
                            // Light commands: write success status
                            writeStatusEntry(wr, ep, cl, cmd, kStatusSuccess);
                        }
                        // Commissioning commands write their own response entry
                    }
                }
            } else {
                if (rd.isStruct() || rd.isArray() || rd.isList()) rd.skipContainer();
            }
        }
    }

    wr.closeContainer();  // /InvokeResponses
    writeIMRevision(wr);
    wr.closeContainer();  // /InvokeResponse

    if (!wr.error())
        transport.sendIM(kOpInvokeResponse, resp, wr.size(),
                         protoHdr.exchangeId, msgHdr.messageCounter);
}

// ---------------------------------------------------------------------------
// SubscribeRequest → ReportData + SubscribeResponse
//
// SubscribeRequestMessage {
//   keepSubscriptions  [0]: BOOL
//   minIntervalFloor   [1]: UINT16
//   maxIntervalCeiling [2]: UINT16
//   attributeRequests  [3]: [AttributePathIB]
//   fabricFiltered     [7]: BOOL
// }
// ---------------------------------------------------------------------------

void MatterIM::handleSubscribeRequest(const uint8_t* payload, size_t len,
                                       const MessageHeader& msgHdr,
                                       const ProtocolHeader& protoHdr,
                                       MatterTransport& transport) {
    _sub.active = true;
    _sub.id = _subIdCounter++;
    _sub.pathCount = 0;

    TLVReader rd(payload, len);
    if (rd.next() && rd.isStruct()) {
        while (rd.next() && !rd.isEnd()) {
            if (rd.tag() == 1) _sub.minIntervalSec = rd.getU16();
            if (rd.tag() == 2) _sub.maxIntervalSec = rd.getU16();
            if (rd.tag() == 3 && rd.isArray()) {
                // tag 3: attributeRequests
                while (rd.next() && !rd.isEnd()) {
                    if (rd.isList() && _sub.pathCount < Subscription::kMaxPaths) {
                        auto& p = _sub.paths[_sub.pathCount];
                        p.active = true;
                        while (rd.next() && !rd.isEnd()) {
                            if (rd.tag() == 2) p.endpoint = rd.getU16();
                            if (rd.tag() == 3) p.cluster = rd.getU32();
                            if (rd.tag() == 4) p.attribute = rd.getU32();
                        }
                        _sub.pathCount++;
                    }
                }
            } else {
                if (rd.isStruct() || rd.isArray() || rd.isList()) rd.skipContainer();
            }
        }
    }

    // Send initial ReportData with all subscribed attributes
    uint8_t report[1024];
    TLVWriter wr(report, sizeof(report));
    wr.openStruct();
    wr.putU32(0, _sub.id);
    wr.openArray(1);

    for (size_t i = 0; i < _sub.pathCount; i++) {
        auto& p = _sub.paths[i];
        if (!p.active || !_clusters) continue;
        wr.openStruct(kAnon);
        wr.openStruct(1);
        wr.putU32(0, 0);
        wr.openList(1);
        wr.putU16(2, p.endpoint);
        wr.putU32(3, p.cluster);
        wr.putU32(4, p.attribute);
        wr.closeContainer();
        _clusters->readAttribute(p.endpoint, p.cluster, p.attribute, wr, 2);
        wr.closeContainer();
        wr.closeContainer();
    }

    wr.closeContainer();
    wr.putBool(4, false);
    writeIMRevision(wr);
    wr.closeContainer();

    transport.sendIM(kOpReportData, report, wr.size(),
                     protoHdr.exchangeId, msgHdr.messageCounter);

    // Defer SubscribeResponse until we receive StatusResponse for the ReportData
    _pendingSub.pending = true;
    _pendingSub.subId = _sub.id;
    _pendingSub.maxInterval = _sub.maxIntervalSec;
    _pendingSub.exchangeId = protoHdr.exchangeId;
    _pendingSub.ackCounter = msgHdr.messageCounter;

    _sub.lastReportMs = millis();
}

void MatterIM::handleStatusResponse(const uint8_t* payload, size_t len,
                                     const MessageHeader& msgHdr,
                                     const ProtocolHeader& protoHdr,
                                     MatterTransport& transport) {
    (void)payload; (void)len;

    // If we have a pending SubscribeResponse, send it now
    if (_pendingSub.pending) {
        uint8_t subResp[36];
        TLVWriter sw(subResp, sizeof(subResp));
        sw.openStruct();
        sw.putU32(0, _pendingSub.subId);
        sw.putU16(2, _pendingSub.maxInterval);  // Spec: MaxInterval = tag 2
        writeIMRevision(sw);
        sw.closeContainer();

        transport.sendIM(kOpSubscribeResponse, subResp, sw.size(),
                         protoHdr.exchangeId, msgHdr.messageCounter);
        _pendingSub.pending = false;
    }
}

// ---------------------------------------------------------------------------
// Subscription report check
// ---------------------------------------------------------------------------

void MatterIM::checkSubscription(MatterTransport& transport) {
    if (!_sub.active || !_clusters) return;

    uint32_t now = millis();
    if (now - _sub.lastReportMs < (uint32_t)_sub.maxIntervalSec * 1000) return;

    uint8_t report[1024];
    TLVWriter wr(report, sizeof(report));
    wr.openStruct();
    wr.putU32(0, _sub.id);
    wr.openArray(1);

    for (size_t i = 0; i < _sub.pathCount; i++) {
        auto& p = _sub.paths[i];
        if (!p.active) continue;
        wr.openStruct(kAnon);
        wr.openStruct(1);
        wr.putU32(0, _clusters->dataVersion);
        wr.openList(1);
        wr.putU16(2, p.endpoint);
        wr.putU32(3, p.cluster);
        wr.putU32(4, p.attribute);
        wr.closeContainer();
        _clusters->readAttribute(p.endpoint, p.cluster, p.attribute, wr, 2);
        wr.closeContainer();
        wr.closeContainer();
    }

    wr.closeContainer();
    wr.putBool(4, false);
    writeIMRevision(wr);
    wr.closeContainer();

    transport.sendIM(kOpReportData, report, wr.size(),
                     transport.nextExchangeId(), 0, true);
    _sub.lastReportMs = now;
}

// ---------------------------------------------------------------------------
// Commissioning cluster commands
//
// These write complete InvokeResponseIB entries (including the wrapper struct).
// Returns true if the command was handled.
// ---------------------------------------------------------------------------

bool MatterIM::handleCommissioningCommand(uint16_t endpoint, uint32_t cluster,
                                           uint32_t command,
                                           TLVReader& rd,
                                           TLVWriter& wr,
                                           MatterTransport& transport) {
    if (endpoint != 0 || !_session) return false;
    (void)transport;

    // --- General Commissioning ---
    if (cluster == kClusterGeneralCommissioning) {
        if (command == kCmdArmFailSafe || command == kCmdSetRegulatoryConfig ||
            command == kCmdCommissioningComplete) {
            rd.skipContainer(); // consume CommandFields
            uint32_t respCmd = command + 1;
            wr.openStruct(kAnon);
            wr.openStruct(0);
            wr.openList(0);
            wr.putU16(0, 0);
            wr.putU32(1, cluster);
            wr.putU32(2, respCmd);
            wr.closeContainer();
            wr.openStruct(1);
            wr.putU8(0, 0);             // ErrorCode = Success
            wr.putString(1, "");        // DebugText
            wr.closeContainer();
            wr.closeContainer();
            wr.closeContainer();
            return true;
        }
    }

    // --- Operational Credentials ---
    if (cluster == kClusterOperationalCredentials) {

        // AttestationRequest: parse tag 0 = attestationNonce (32 bytes)
        if (command == kCmdAttestationRequest) {
            uint8_t attestNonce[32] = {};
            while (rd.next() && !rd.isEnd()) {
                if (rd.tag() == 0) {
                    size_t n;
                    const uint8_t* d = rd.getBytes(n);
                    if (n == 32) memcpy(attestNonce, d, 32);
                }
            }

            uint8_t elemBuf[512];
            TLVWriter ew(elemBuf, sizeof(elemBuf));
            ew.openStruct();
            ew.putBytes(1, kTestCD, kTestCDLen);
            ew.putBytes(2, attestNonce, 32);
            ew.putU32(3, 0);  // Timestamp
            ew.closeContainer();

            uint8_t tbs[600];
            memcpy(tbs, elemBuf, ew.size());
            memcpy(tbs + ew.size(), _session->attestChallenge(), 16);

            uint8_t sig[64];
            size_t sigLen;
            ecdsaSign(kTestDACKey, tbs, ew.size() + 16, sig, sigLen);

            wr.openStruct(kAnon);
            wr.openStruct(0);
            wr.openList(0);
            wr.putU16(0, 0);
            wr.putU32(1, cluster);
            wr.putU32(2, kCmdAttestationResponse);
            wr.closeContainer();
            wr.openStruct(1);
            wr.putBytes(0, elemBuf, ew.size());
            wr.putBytes(1, sig, sigLen);
            wr.closeContainer();
            wr.closeContainer();
            wr.closeContainer();
            return true;
        }

        // CertificateChainRequest: parse tag 0 = certificateType (1=DAC, 2=PAI)
        if (command == kCmdCertChainRequest) {
            uint8_t certType = 1;
            while (rd.next() && !rd.isEnd()) {
                if (rd.tag() == 0) certType = rd.getU8();
            }

            wr.openStruct(kAnon);
            wr.openStruct(0);
            wr.openList(0);
            wr.putU16(0, 0);
            wr.putU32(1, cluster);
            wr.putU32(2, kCmdCertChainResponse);
            wr.closeContainer();
            wr.openStruct(1);
            if (certType == 2)
                wr.putBytes(0, kTestPAICert, kTestPAICertLen);
            else
                wr.putBytes(0, kTestDACCert, kTestDACCertLen);
            wr.closeContainer();
            wr.closeContainer();
            wr.closeContainer();
            return true;
        }

        // CSRRequest: parse tag 0 = CSRNonce (32 bytes)
        if (command == kCmdCSRRequest) {
            uint8_t csrNonce[32] = {};
            while (rd.next() && !rd.isEnd()) {
                if (rd.tag() == 0) {
                    size_t n;
                    const uint8_t* d = rd.getBytes(n);
                    if (n == 32) memcpy(csrNonce, d, 32);
                }
            }

            ecGenerateKeypair(_session->fabric().operationalKey);

            uint8_t csrBuf[kMaxCSRSize];
            size_t csrLen;
            generateCSR(_session->fabric().operationalKey, csrBuf, csrLen);

            uint8_t nocsrElem[600];
            TLVWriter nw(nocsrElem, sizeof(nocsrElem));
            nw.openStruct();
            nw.putBytes(1, csrBuf, csrLen);
            nw.putBytes(2, csrNonce, 32);
            nw.closeContainer();

            uint8_t tbs[700];
            memcpy(tbs, nocsrElem, nw.size());
            memcpy(tbs + nw.size(), _session->attestChallenge(), 16);

            uint8_t sig[64];
            size_t sigLen;
            ecdsaSign(kTestDACKey, tbs, nw.size() + 16, sig, sigLen);

            wr.openStruct(kAnon);
            wr.openStruct(0);
            wr.openList(0);
            wr.putU16(0, 0);
            wr.putU32(1, cluster);
            wr.putU32(2, kCmdCSRResponse);
            wr.closeContainer();
            wr.openStruct(1);
            wr.putBytes(0, nocsrElem, nw.size());
            wr.putBytes(1, sig, sigLen);
            wr.closeContainer();
            wr.closeContainer();
            wr.closeContainer();
            return true;
        }

        // AddNOC: parse tag 0=NOC, 1=ICAC (opt), 2=IPKValue, 3=caseAdminSubject, 4=adminVendorId
        if (command == kCmdAddNOC) {
            FabricInfo& fab = _session->fabric();
            uint64_t caseAdminSubject = 0;
            const uint8_t* nocData = nullptr;
            size_t nocLen = 0;
            const uint8_t* icacData = nullptr;
            size_t icacLen = 0;

            while (rd.next() && !rd.isEnd()) {
                size_t n;
                const uint8_t* d;
                switch (rd.tag()) {
                    case 0: // NOC
                        nocData = rd.getBytes(nocLen);
                        break;
                    case 1: // ICAC (optional)
                        icacData = rd.getBytes(icacLen);
                        break;
                    case 2: // IPKValue (16 bytes)
                        d = rd.getBytes(n);
                        if (n == 16) memcpy(fab.ipk, d, 16);
                        break;
                    case 3: // CaseAdminSubject
                        caseAdminSubject = rd.getU64();
                        break;
                    default: break;
                }
            }

            // Save certs to flash
            if (nocData && nocLen > 0 && nocLen <= kMaxCertSize) {
                FabricStore::saveCert("noc", nocData, nocLen);
                fab.valid = true;
            }
            if (icacData && icacLen > 0 && icacLen <= kMaxCertSize) {
                FabricStore::saveCert("icac", icacData, icacLen);
            }

            if (fab.valid) _clusters->commissioned = true;

            // Extract nodeId and fabricId from the NOC subject DN
            uint64_t nocNodeId = 0, nocFabricId = 0;
            if (nocData && extractIdsFromCert(nocData, nocLen, nocNodeId, nocFabricId)) {
                fab.nodeId = nocNodeId;
                fab.fabricId = nocFabricId;
            } else {
                fab.nodeId = caseAdminSubject ? caseAdminSubject : 1;
                fab.fabricId = 1;
            }

            // Persist core fabric data (key, IPK, IDs)
            FabricStore::saveCore(fab);

            wr.openStruct(kAnon);
            wr.openStruct(0);
            wr.openList(0);
            wr.putU16(0, 0);
            wr.putU32(1, cluster);
            wr.putU32(2, kCmdNOCResponse);
            wr.closeContainer();
            wr.openStruct(1);
            wr.putU8(0, 0);        // StatusCode: Success
            wr.putU8(1, 1);        // FabricIndex
            wr.putString(2, "");   // DebugText
            wr.closeContainer();
            wr.closeContainer();
            wr.closeContainer();
            return true;
        }

        // AddTrustedRootCertificate: parse tag 0 = RootCACertificate
        if (command == kCmdAddTrustedRootCert) {
            while (rd.next() && !rd.isEnd()) {
                if (rd.tag() == 0) {
                    size_t n;
                    const uint8_t* d = rd.getBytes(n);
                    if (n > 0 && n <= kMaxCertSize) {
                        FabricStore::saveCert("root", d, n);
                    }
                }
            }
            writeStatusEntry(wr, endpoint, cluster, command, kStatusSuccess);
            return true;
        }
    }

    rd.skipContainer(); // consume unhandled CommandFields
    return false;
}

} // namespace matter
