#include "MatterSession.h"
#include "MatterTLV.h"
#include "MatterTransport.h"
#include <string.h>

namespace matter {

void MatterSession::begin(uint32_t passcode, uint16_t discriminator) {
    _passcode = passcode;
    _discriminator = discriminator;
    memset(&_sessionKeys, 0, sizeof(_sessionKeys));
    memset(&_spake, 0, sizeof(_spake));
    _contextLen = 0;
    randomBytes(_salt, kPBKDF2SaltSize);

    // Try to load persisted fabric data (survives reboot)
    if (FabricStore::loadCore(_fabric)) {
        _state = CASE_WAIT_SIGMA1;  // Commissioned — wait for CASE
    } else {
        _state = PASE_WAIT_PBKDF_REQ;  // Not commissioned — wait for PASE
    }
}

// ---------------------------------------------------------------------------
// Exchange timeout — reset stuck handshakes
// ---------------------------------------------------------------------------

void MatterSession::checkTimeout() {
    // Only timeout mid-handshake states
    bool inHandshake = (_state == PASE_WAIT_PAKE1 || _state == PASE_WAIT_PAKE3 ||
                        _state == CASE_WAIT_SIGMA3);
    if (!inHandshake || _exchangeStartMs == 0) return;

#ifndef NATIVE_TEST
    uint32_t now = ::millis();
    if ((now - _exchangeStartMs) >= kExchangeTimeoutMs) {
        // Reset to initial state
        if (_state == CASE_WAIT_SIGMA3) {
            _caseTranscript.release();
            _state = _fabric.valid ? CASE_WAIT_SIGMA1 : PASE_WAIT_PBKDF_REQ;
        } else {
            openCommissioning();
        }
        _exchangeStartMs = 0;
    }
#endif
}

// ---------------------------------------------------------------------------
// Secure Channel dispatch
// ---------------------------------------------------------------------------

bool MatterSession::handleSecureChannel(uint8_t opcode,
                                         const uint8_t* payload, size_t payloadLen,
                                         const MessageHeader& msgHdr,
                                         const ProtocolHeader& protoHdr,
                                         MatterTransport& transport) {
    switch (opcode) {
        case kOpPBKDFParamRequest:
            // Per Matter Spec 4.13: a new PBKDFParamRequest restarts PASE
            // regardless of current state (allows commissioner retry)
            if (_state == PASE_WAIT_PBKDF_REQ ||
                _state == PASE_WAIT_PAKE1 ||
                _state == PASE_WAIT_PAKE3) {
                // Reset PASE state for fresh attempt
                if (_state != PASE_WAIT_PBKDF_REQ) {
                    _contextLen = 0;
                    memset(&_spake, 0, sizeof(_spake));
                    memset(&_sessionKeys, 0, sizeof(_sessionKeys));
                }
                handlePBKDFParamReq(payload, payloadLen, msgHdr, protoHdr, transport);
            }
            return true;
        case kOpPasePake1:
            if (_state == PASE_WAIT_PAKE1)
                handlePake1(payload, payloadLen, msgHdr, protoHdr, transport);
            return true;
        case kOpPasePake3:
            if (_state == PASE_WAIT_PAKE3)
                handlePake3(payload, payloadLen, msgHdr, protoHdr, transport);
            return true;
        case kOpCaseSigma1:
            if (_fabric.valid)
                handleSigma1(payload, payloadLen, msgHdr, protoHdr, transport);
            return true;
        case kOpCaseSigma3:
            if (_state == CASE_WAIT_SIGMA3)
                handleSigma3(payload, payloadLen, msgHdr, protoHdr, transport);
            return true;
        case kOpStatusReport: {
            // Parse StatusReport to check for errors
            StatusReport sr;
            if (payloadLen >= 8) sr.decode(payload, payloadLen);
            if (sr.generalCode != kGeneralSuccess) {
                // Peer reported error — reset handshake state
                if (_state == PASE_WAIT_PAKE1 || _state == PASE_WAIT_PAKE3) {
                    openCommissioning();
                } else if (_state == CASE_WAIT_SIGMA3) {
                    _caseTranscript.release();
                    _state = _fabric.valid ? CASE_WAIT_SIGMA1 : PASE_WAIT_PBKDF_REQ;
                }
                _exchangeStartMs = 0;
            }
            return true;
        }
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// PASE: PBKDFParamRequest → PBKDFParamResponse
// ---------------------------------------------------------------------------

void MatterSession::handlePBKDFParamReq(const uint8_t* payload, size_t len,
                                         const MessageHeader& msgHdr,
                                         const ProtocolHeader& protoHdr,
                                         MatterTransport& transport) {
    // Clear previous session state
    memset(&_sessionKeys, 0, sizeof(_sessionKeys));
    memset(&_spake, 0, sizeof(_spake));

    // Parse request TLV
    TLVReader rd(payload, len);
    uint8_t initiatorRandom[32] = {};
    uint16_t initiatorSessionId = 0;

    while (rd.next()) {
        if (rd.isEnd()) break;
        switch (rd.tag()) {
            case 1: {
                size_t n; const uint8_t* d = rd.getBytes(n);
                if (n >= 32) memcpy(initiatorRandom, d, 32);
                break;
            }
            case 2: initiatorSessionId = rd.getU16(); break;
            default: break;
        }
    }

    _peerSessionId = initiatorSessionId;
    _paseExchangeId = protoHdr.exchangeId;

    // Save request for context hash (bounded)
    size_t copyLen = len < sizeof(_contextBuf) ? len : sizeof(_contextBuf);
    memcpy(_contextBuf, payload, copyLen);
    _contextLen = copyLen;

    // Build response
    uint8_t resp[256];
    TLVWriter wr(resp, sizeof(resp));
    wr.openStruct();

    // tag 1: initiatorRandom (echo)
    wr.putBytes(1, initiatorRandom, 32);

    // tag 2: responderRandom
    uint8_t responderRandom[32];
    randomBytes(responderRandom, 32);
    wr.putBytes(2, responderRandom, 32);

    // tag 3: responderSessionId
    wr.putU16(3, _localSessionId);

    // tag 4: pbkdf_parameters
    wr.openStruct(4);
    wr.putU32(1, kPBKDF2Iterations);
    wr.putBytes(2, _salt, kPBKDF2SaltSize);
    wr.closeContainer();

    wr.closeContainer();

    // Append response to context
    if (_contextLen + wr.size() <= sizeof(_contextBuf)) {
        memcpy(_contextBuf + _contextLen, resp, wr.size());
        _contextLen += wr.size();
    }

    // Derive w0, w1
    spake2pDeriveW0W1(_passcode, _salt, kPBKDF2SaltSize, kPBKDF2Iterations,
                      _spake.w0, _spake.w1);

    transport.sendSecureChannel(kOpPBKDFParamResponse, resp, wr.size(),
                                protoHdr.exchangeId, msgHdr.messageCounter);
    _state = PASE_WAIT_PAKE1;
#ifndef NATIVE_TEST
    _exchangeStartMs = ::millis();
#endif
}

// ---------------------------------------------------------------------------
// PASE: Pake1 → Pake2
// ---------------------------------------------------------------------------

void MatterSession::handlePake1(const uint8_t* payload, size_t len,
                                 const MessageHeader& msgHdr,
                                 const ProtocolHeader& protoHdr,
                                 MatterTransport& transport) {
    // Parse Pake1: tag 1 = pA (65 bytes)
    TLVReader rd(payload, len);
    while (rd.next()) {
        if (rd.tag() == 1) {
            size_t n;
            const uint8_t* pA = rd.getBytes(n);
            if (n == kP256PubKeySize) {
                memcpy(_spake.pA, pA, kP256PubKeySize);
            }
            break;
        }
    }

    // Build PASE context: SHA-256("CHIP PAKE V1 Commissioning" || req || resp)
    static const char kPakePrefix[] = "CHIP PAKE V1 Commissioning";
    uint8_t paseContext[kSHA256Size];
    {
        // Incremental SHA-256 of prefix + PBKDFParamReq + PBKDFParamResp
        uint8_t tmp[600];
        size_t tmpLen = 0;
        memcpy(tmp, kPakePrefix, 26);
        tmpLen += 26;
        memcpy(tmp + tmpLen, _contextBuf, _contextLen);
        tmpLen += _contextLen;
        sha256(tmp, tmpLen, paseContext);
    }

    // Compute SPAKE2+ response (pass 32-byte context hash)
    if (!spake2pComputeResponse(_spake, paseContext, kSHA256Size)) {
        StatusReport sr;
        sr.generalCode = kGeneralFailure;
        sr.protocolId = kProtoSecureChannel;
        sr.protocolCode = kProtoCodeInvalidParam;
        uint8_t errBuf[8];
        size_t errLen = sr.encode(errBuf, sizeof(errBuf));
        transport.sendSecureChannel(kOpStatusReport, errBuf, errLen,
                                    protoHdr.exchangeId, msgHdr.messageCounter);
        _state = PASE_WAIT_PBKDF_REQ;
        return;
    }

    // Build Pake2 response
    uint8_t resp[256];
    TLVWriter wr(resp, sizeof(resp));
    wr.openStruct();
    wr.putBytes(1, _spake.pB, kP256PubKeySize);
    wr.putBytes(2, _spake.cB, kHMACSize);
    wr.closeContainer();

    transport.sendSecureChannel(kOpPasePake2, resp, wr.size(),
                                protoHdr.exchangeId, msgHdr.messageCounter);
    _state = PASE_WAIT_PAKE3;
}

// ---------------------------------------------------------------------------
// PASE: Pake3 → StatusReport → Session Active
// ---------------------------------------------------------------------------

void MatterSession::handlePake3(const uint8_t* payload, size_t len,
                                 const MessageHeader& msgHdr,
                                 const ProtocolHeader& protoHdr,
                                 MatterTransport& transport) {
    // Parse Pake3: tag 1 = cA (32 bytes)
    TLVReader rd(payload, len);
    const uint8_t* cA = nullptr;
    size_t cALen = 0;
    while (rd.next()) {
        if (rd.tag() == 1) {
            cA = rd.getBytes(cALen);
            break;
        }
    }

    if (!cA || !spake2pVerify(_spake, cA, cALen)) {
        // Send failure StatusReport
        StatusReport sr;
        sr.generalCode = kGeneralFailure;
        sr.protocolId = kProtoSecureChannel;
        sr.protocolCode = kProtoCodeInvalidParam;
        uint8_t buf[8];
        size_t n = sr.encode(buf, sizeof(buf));
        transport.sendSecureChannel(kOpStatusReport, buf, n,
                                    protoHdr.exchangeId, msgHdr.messageCounter);
        _state = PASE_WAIT_PBKDF_REQ;
        return;
    }

    // Derive session keys from Ke (16 bytes, per SPAKE2+ draft-01)
    deriveSessionKeys(_spake.Ke, kAES128KeySize, nullptr, 0,
                      "SessionKeys", _sessionKeys);

    // Send success StatusReport
    StatusReport sr;
    sr.generalCode = kGeneralSuccess;
    sr.protocolId = kProtoSecureChannel;
    sr.protocolCode = kProtoCodeSessionEstablished;
    uint8_t buf[8];
    size_t n = sr.encode(buf, sizeof(buf));
    transport.sendSecureChannel(kOpStatusReport, buf, n,
                                protoHdr.exchangeId, msgHdr.messageCounter);

    _state = PASE_ACTIVE;
    _exchangeStartMs = 0;
}

// ---------------------------------------------------------------------------
// CASE: Sigma1 → Sigma2
// ---------------------------------------------------------------------------

void MatterSession::handleSigma1(const uint8_t* payload, size_t len,
                                  const MessageHeader& msgHdr,
                                  const ProtocolHeader& protoHdr,
                                  MatterTransport& transport) {
    // Clear previous session state
    memset(&_sessionKeys, 0, sizeof(_sessionKeys));
    memset(_caseSharedSecret, 0, sizeof(_caseSharedSecret));

    // Start incremental transcript hash
    _caseTranscript.begin();

    // Parse Sigma1
    TLVReader rd(payload, len);
    uint8_t initiatorRandom[32] = {};
    uint16_t initiatorSessionId = 0;
    uint8_t initiatorEphPub[kP256PubKeySize] = {};

    while (rd.next()) {
        if (rd.isEnd()) break;
        size_t n;
        switch (rd.tag()) {
            case 1: {
                const uint8_t* d = rd.getBytes(n);
                if (n >= 32) memcpy(initiatorRandom, d, 32);
                break;
            }
            case 2: initiatorSessionId = rd.getU16(); break;
            case 4: {
                const uint8_t* key = rd.getBytes(n);
                if (n == kP256PubKeySize) memcpy(initiatorEphPub, key, n);
                break;
            }
            default: break;
        }
    }

    _peerSessionId = initiatorSessionId;
    _localSessionId++;
    if (_localSessionId == kUnsecuredSessionId) _localSessionId = 1;

    // Save initiator ephemeral public key for Sigma3 TBS verification
    memcpy(_caseInitiatorEphPub, initiatorEphPub, kP256PubKeySize);

    // Feed Sigma1 into transcript
    _caseTranscript.update(payload, len);

    // Generate ephemeral key pair
    if (!ecGenerateKeypair(_caseEphKey)) { _caseTranscript.release(); return; }

    // Compute ECDHE shared secret
    size_t secretLen;
    if (!ecdhSharedSecret(_caseEphKey.priv, initiatorEphPub,
                          _caseSharedSecret, secretLen)) { _caseTranscript.release(); return; }

    // Generate responderRandom early (needed for S2K salt)
    uint8_t responderRandom[32];
    randomBytes(responderRandom, 32);

    // Load NOC and ICAC from flash (not kept in RAM)
    uint8_t noc[kMaxCertSize];
    size_t nocLen = FabricStore::loadCert("noc", noc, sizeof(noc));
    uint8_t icac[kMaxCertSize];
    size_t icacLen = FabricStore::loadCert("icac", icac, sizeof(icac));

    if (nocLen == 0) { _caseTranscript.release(); return; }

    // Build TBS (To Be Signed) for Sigma2
    uint8_t tbs[512];
    TLVWriter tbsW(tbs, sizeof(tbs));
    tbsW.openStruct();
    tbsW.putBytes(1, noc, nocLen);
    if (icacLen > 0) tbsW.putBytes(2, icac, icacLen);
    tbsW.putBytes(3, _caseEphKey.pub, kP256PubKeySize);
    tbsW.putBytes(4, initiatorEphPub, kP256PubKeySize);
    tbsW.closeContainer();

    // Sign TBS with operational key
    uint8_t sig[64];
    size_t sigLen;
    if (!ecdsaSign(_fabric.operationalKey.priv, tbs, tbsW.size(), sig, sigLen)) { _caseTranscript.release(); return; }

    // Build TBEData2 plaintext: {NOC[1], ICAC[2]?, signature[3], resumptionId[4]}
    uint8_t plain2[768];
    TLVWriter p2w(plain2, sizeof(plain2));
    p2w.openStruct();
    p2w.putBytes(1, noc, nocLen);
    if (icacLen > 0) p2w.putBytes(2, icac, icacLen);
    p2w.putBytes(3, sig, sigLen);
    uint8_t resumptionId[16];
    randomBytes(resumptionId, 16);
    p2w.putBytes(4, resumptionId, 16);
    p2w.closeContainer();

    // Derive S2K: salt = IPK(16) || responderRandom(32) || respEphPub(65) || hash(Sigma1)(32)
    uint8_t hashAfterSigma1[kSHA256Size];
    _caseTranscript.snapshot(hashAfterSigma1);

    uint8_t s2kSalt[16 + 32 + kP256PubKeySize + kSHA256Size]; // 145 bytes
    size_t spos = 0;
    memcpy(s2kSalt + spos, _fabric.ipk, 16);            spos += 16;
    memcpy(s2kSalt + spos, responderRandom, 32);         spos += 32;
    memcpy(s2kSalt + spos, _caseEphKey.pub, kP256PubKeySize); spos += kP256PubKeySize;
    memcpy(s2kSalt + spos, hashAfterSigma1, kSHA256Size); spos += kSHA256Size;

    uint8_t s2k[kAES128KeySize];
    hkdfSha256(s2kSalt, spos, _caseSharedSecret, secretLen,
               (const uint8_t*)"Sigma2", 6, s2k, kAES128KeySize);

    // Encrypt TBEData2 with AES-CCM
    static const uint8_t s2Nonce[kCCMNonceSize] = {
        'N','C','A','S','E','_','S','i','g','m','a','2','N'
    };
    uint8_t encrypted2[800];
    uint8_t tag2[kCCMTagSize];
    aesCcmEncrypt(s2k, s2Nonce, kCCMNonceSize, nullptr, 0,
                  plain2, p2w.size(), encrypted2, tag2);

    // Build Sigma2 response TLV
    uint8_t resp[1024];
    TLVWriter wr(resp, sizeof(resp));
    wr.openStruct();
    wr.putBytes(1, responderRandom, 32);
    wr.putU16(2, _localSessionId);
    wr.putBytes(3, _caseEphKey.pub, kP256PubKeySize);

    // encrypted2 = ciphertext + tag
    uint8_t enc2Combined[816];
    memcpy(enc2Combined, encrypted2, p2w.size());
    memcpy(enc2Combined + p2w.size(), tag2, kCCMTagSize);
    wr.putBytes(4, enc2Combined, p2w.size() + kCCMTagSize);

    wr.closeContainer();

    // Feed Sigma2 into transcript
    _caseTranscript.update(resp, wr.size());

    transport.sendSecureChannel(kOpCaseSigma2, resp, wr.size(),
                                protoHdr.exchangeId, msgHdr.messageCounter);

    _state = CASE_WAIT_SIGMA3;
#ifndef NATIVE_TEST
    _exchangeStartMs = ::millis();
#endif
}

// ---------------------------------------------------------------------------
// CASE: Sigma3 → Session Active
// ---------------------------------------------------------------------------

void MatterSession::handleSigma3(const uint8_t* payload, size_t len,
                                  const MessageHeader& msgHdr,
                                  const ProtocolHeader& protoHdr,
                                  MatterTransport& transport) {
    // Parse Sigma3: tag 1 = encrypted3
    TLVReader rd(payload, len);
    const uint8_t* encrypted3 = nullptr;
    size_t enc3Len = 0;
    while (rd.next()) {
        if (rd.tag() == 1) {
            encrypted3 = rd.getBytes(enc3Len);
            break;
        }
    }
    if (!encrypted3 || enc3Len <= kCCMTagSize) {
        _caseTranscript.release();
        _state = CASE_WAIT_SIGMA1;
        return;
    }

    // Decrypt
    size_t cipherLen = enc3Len - kCCMTagSize;
    const uint8_t* tag3 = encrypted3 + cipherLen;

    // S3K salt = IPK(16) || transcriptHash(Sigma1||Sigma2)(32)
    uint8_t hashAfterSigma2[kSHA256Size];
    _caseTranscript.snapshot(hashAfterSigma2);

    uint8_t s3kSalt[16 + kSHA256Size]; // 48 bytes
    memcpy(s3kSalt, _fabric.ipk, 16);
    memcpy(s3kSalt + 16, hashAfterSigma2, kSHA256Size);

    uint8_t s3k[kAES128KeySize];
    hkdfSha256(s3kSalt, sizeof(s3kSalt), _caseSharedSecret, kP256PrivKeySize,
               (const uint8_t*)"Sigma3", 6, s3k, kAES128KeySize);

    static const uint8_t s3Nonce[kCCMNonceSize] = {
        'N','C','A','S','E','_','S','i','g','m','a','3','N'
    };
    uint8_t plain3[768];
    if (!aesCcmDecrypt(s3k, s3Nonce, kCCMNonceSize, nullptr, 0,
                       encrypted3, cipherLen, tag3, plain3)) {
        _caseTranscript.release();
        _state = CASE_WAIT_SIGMA1;
        return;
    }

    // Parse plain3 TLV: {NOC[1], ICAC[2] (optional), signature[3]}
    TLVReader rd3(plain3, cipherLen);
    const uint8_t* initNoc = nullptr;
    size_t initNocLen = 0;
    const uint8_t* initSig = nullptr;
    size_t initSigLen = 0;

    if (rd3.next() && rd3.isStruct()) {
        while (rd3.next() && !rd3.isEnd()) {
            size_t n;
            switch (rd3.tag()) {
                case 1: initNoc = rd3.getBytes(n); initNocLen = n; break;
                case 3: initSig = rd3.getBytes(n); initSigLen = n; break;
                default: break;
            }
        }
    }

    if (!initNoc || !initSig || initSigLen != 64) {
        _caseTranscript.release();
        _state = CASE_WAIT_SIGMA1;
        return;
    }

    // Extract initiator's public key from their NOC
    uint8_t initPubKey[kP256PubKeySize];
    if (!extractPubKeyFromCert(initNoc, initNocLen, initPubKey)) {
        _caseTranscript.release();
        _state = CASE_WAIT_SIGMA1;
        return;
    }

    // Build TBS3 = TLV{NOC[1], initiatorEphPub[3], responderEphPub[4]}
    uint8_t tbs3[768];
    TLVWriter tbsW(tbs3, sizeof(tbs3));
    tbsW.openStruct();
    tbsW.putBytes(1, initNoc, initNocLen);
    tbsW.putBytes(3, _caseInitiatorEphPub, kP256PubKeySize);
    tbsW.putBytes(4, _caseEphKey.pub, kP256PubKeySize);
    tbsW.closeContainer();

    // Verify signature over TBS3 with initiator's NOC public key
    if (!ecdsaVerify(initPubKey, tbs3, tbsW.size(), initSig, initSigLen)) {
        _caseTranscript.release();
        _state = CASE_WAIT_SIGMA1;
        return;
    }

    // Feed Sigma3 into transcript, then finalize
    _caseTranscript.update(payload, len);

    uint8_t hashFinal[kSHA256Size];
    _caseTranscript.finish(hashFinal);

    // Derive session keys: salt = IPK(16) || transcriptHash(Sigma1||Sigma2||Sigma3)(32)
    uint8_t sesSalt[16 + kSHA256Size]; // 48 bytes
    memcpy(sesSalt, _fabric.ipk, 16);
    memcpy(sesSalt + 16, hashFinal, kSHA256Size);

    deriveSessionKeys(_caseSharedSecret, kP256PrivKeySize,
                      sesSalt, sizeof(sesSalt), "SessionKeys", _sessionKeys);

    // Send success StatusReport
    StatusReport sr;
    sr.generalCode = kGeneralSuccess;
    sr.protocolId = kProtoSecureChannel;
    sr.protocolCode = kProtoCodeSessionEstablished;
    uint8_t buf[8];
    size_t n = sr.encode(buf, sizeof(buf));
    transport.sendSecureChannel(kOpStatusReport, buf, n,
                                protoHdr.exchangeId, msgHdr.messageCounter);

    _state = CASE_ACTIVE;
    _exchangeStartMs = 0;
}

// ---------------------------------------------------------------------------
// Message encryption / decryption
// ---------------------------------------------------------------------------

void MatterSession::buildNonce(uint8_t secFlags, uint32_t msgCounter,
                                uint64_t nodeId, uint8_t* nonce) const {
    // Nonce: securityFlags(1) || messageCounter(4 LE) || sourceNodeId(8 LE) = 13 bytes
    nonce[0] = secFlags;
    nonce[1] = msgCounter & 0xFF;
    nonce[2] = (msgCounter >> 8) & 0xFF;
    nonce[3] = (msgCounter >> 16) & 0xFF;
    nonce[4] = (msgCounter >> 24) & 0xFF;
    for (int i = 0; i < 8; i++) nonce[5 + i] = (nodeId >> (i * 8)) & 0xFF;
}

size_t MatterSession::encrypt(const uint8_t* aad, size_t aadLen,
                               const uint8_t* plain, size_t plainLen,
                               uint32_t msgCounter,
                               uint8_t* output) {
    uint8_t nonce[kCCMNonceSize];
    // Security flags for R2I (device→controller): no special flags set
    buildNonce(0x00, msgCounter, _fabric.nodeId, nonce);

    uint8_t tag[kCCMTagSize];
    if (!aesCcmEncrypt(_sessionKeys.r2iKey, nonce, kCCMNonceSize,
                       aad, aadLen, plain, plainLen,
                       output, tag)) return 0;

    memcpy(output + plainLen, tag, kCCMTagSize);
    return plainLen + kCCMTagSize;
}

size_t MatterSession::decrypt(const MessageHeader& hdr,
                               const uint8_t* ciphertext, size_t cipherLen,
                               uint8_t* plain) {
    if (cipherLen <= kCCMTagSize) return 0;

    size_t payloadLen = cipherLen - kCCMTagSize;
    const uint8_t* tag = ciphertext + payloadLen;

    // AAD = encoded message header
    uint8_t aad[32];
    size_t aadLen = hdr.encode(aad, sizeof(aad));

    uint8_t nonce[kCCMNonceSize];
    uint64_t srcNode = hdr.hasSrc ? hdr.sourceNodeId : 0;
    buildNonce(hdr.securityFlags, hdr.messageCounter, srcNode, nonce);

    if (!aesCcmDecrypt(_sessionKeys.i2rKey, nonce, kCCMNonceSize,
                       aad, aadLen, ciphertext, payloadLen,
                       tag, plain)) return 0;

    return payloadLen;
}

} // namespace matter
