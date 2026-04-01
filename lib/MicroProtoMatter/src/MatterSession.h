#pragma once
#include <stdint.h>
#include <string.h>
#include "MatterConst.h"
#include "MatterCrypto.h"
#include "MatterMessage.h"

namespace matter {

// Forward declare — MatterTransport provides sendMessage()
class MatterTransport;

// ---------------------------------------------------------------------------
// FabricStore — persists fabric certs/keys to NVS flash
//
// Certs (NOC, ICAC, root) live only in flash after commissioning.
// Small fields (key, IPK, IDs) are cached in FabricInfo for fast access.
// ---------------------------------------------------------------------------
class FabricStore {
public:
    // Save a cert/blob to NVS by key name
    static bool saveCert(const char* key, const uint8_t* data, size_t len);
    // Load a cert/blob from NVS. Returns bytes read, 0 on failure.
    static size_t loadCert(const char* key, uint8_t* buf, size_t bufSize);
    // Save the small fields (key, IPK, IDs, valid flag)
    static bool saveCore(const struct FabricInfo& fab);
    // Load the small fields
    static bool loadCore(struct FabricInfo& fab);
    // Erase all fabric data
    static bool erase();
};

// ---------------------------------------------------------------------------
// FabricInfo — installed during commissioning, used for CASE
// Certs are stored in flash via FabricStore, not in RAM.
// ---------------------------------------------------------------------------
struct FabricInfo {
    bool     valid = false;
    ECKeyPair operationalKey;                // Generated during CSRRequest
    uint64_t fabricId = 0;
    uint64_t nodeId = 0;
    uint8_t  ipk[16] = {};                   // Identity Protection Key
};

// ---------------------------------------------------------------------------
// MatterSession — handles PASE and CASE session establishment
// ---------------------------------------------------------------------------
class MatterSession {
public:
    enum State {
        IDLE,
        // PASE flow
        PASE_WAIT_PBKDF_REQ,
        PASE_WAIT_PAKE1,
        PASE_WAIT_PAKE3,
        PASE_ACTIVE,
        // CASE flow
        CASE_WAIT_SIGMA1,
        CASE_WAIT_SIGMA3,
        CASE_ACTIVE,
    };

    void begin(uint32_t passcode, uint16_t discriminator);
    State state() const { return _state; }
    bool isSecure() const { return _state == PASE_ACTIVE || _state == CASE_ACTIVE; }
    bool isCommissioned() const { return _fabric.valid; }

    // Process a Secure Channel message. Returns true if handled.
    bool handleSecureChannel(uint8_t opcode,
                             const uint8_t* payload, size_t payloadLen,
                             const MessageHeader& msgHdr,
                             const ProtocolHeader& protoHdr,
                             MatterTransport& transport);

    // Encrypt outgoing payload (proto header + data). Returns total size incl tag.
    size_t encrypt(const uint8_t* aad, size_t aadLen,
                   const uint8_t* plain, size_t plainLen,
                   uint32_t msgCounter,
                   uint8_t* output);

    // Decrypt incoming payload. Returns plaintext length, 0 on failure.
    size_t decrypt(const MessageHeader& hdr,
                   const uint8_t* ciphertext, size_t cipherLen,
                   uint8_t* plain);

    // Session IDs
    uint16_t localSessionId() const { return _localSessionId; }
    uint16_t peerSessionId()  const { return _peerSessionId; }
    uint64_t nodeId()         const { return _fabric.nodeId; }

    // Access fabric (needed by IM for commissioning cluster commands)
    FabricInfo& fabric() { return _fabric; }

    // Attestation challenge (needed by commissioning)
    const uint8_t* attestChallenge() const { return _sessionKeys.attestChallenge; }

    // Open commissioning window (allow PASE again)
    // Fully resets PASE state so a new PBKDFParamRequest can be handled cleanly
    void openCommissioning() {
        _state = PASE_WAIT_PBKDF_REQ;
        _peerSessionId = 0;
        _paseExchangeId = 0;
        _contextLen = 0;
        _sessionKeys = {};
        memset(&_spake, 0, sizeof(_spake));
        memset(_contextBuf, 0, sizeof(_contextBuf));
        // Generate fresh salt for next PASE (same as begin())
        randomBytes(_salt, kPBKDF2SaltSize);
    }

private:
    // PASE handlers
    void handlePBKDFParamReq(const uint8_t* payload, size_t len,
                             const MessageHeader& msgHdr,
                             const ProtocolHeader& protoHdr,
                             MatterTransport& transport);
    void handlePake1(const uint8_t* payload, size_t len,
                     const MessageHeader& msgHdr,
                     const ProtocolHeader& protoHdr,
                     MatterTransport& transport);
    void handlePake3(const uint8_t* payload, size_t len,
                     const MessageHeader& msgHdr,
                     const ProtocolHeader& protoHdr,
                     MatterTransport& transport);

    // CASE handlers
    void handleSigma1(const uint8_t* payload, size_t len,
                      const MessageHeader& msgHdr,
                      const ProtocolHeader& protoHdr,
                      MatterTransport& transport);
    void handleSigma3(const uint8_t* payload, size_t len,
                      const MessageHeader& msgHdr,
                      const ProtocolHeader& protoHdr,
                      MatterTransport& transport);

    // Build nonce for message encryption/decryption
    void buildNonce(uint8_t secFlags, uint32_t msgCounter, uint64_t nodeId,
                    uint8_t* nonce) const;

    State       _state = IDLE;
    uint32_t    _passcode = kTestPasscode;
    uint16_t    _discriminator = kTestDiscriminator;
    uint16_t    _localSessionId = 1;
    uint16_t    _peerSessionId = 0;
    uint16_t    _paseExchangeId = 0;
    SessionKeys _sessionKeys = {};
    FabricInfo  _fabric;

    // PASE state (only valid during commissioning)
    Spake2pState _spake = {};
    uint8_t      _salt[kPBKDF2SaltSize] = {};
    uint8_t      _contextBuf[512] = {};  // PBKDFParamReq || PBKDFParamResp
    size_t       _contextLen = 0;

    // Fail-safe timer (set by ArmFailSafe, checked in loop)
    uint32_t    _failSafeExpiryMs = 0;
    bool        _failSafeArmed = false;

    // CASE state
    ECKeyPair   _caseEphKey;
    uint8_t     _caseSharedSecret[kP256PrivKeySize] = {};
    Sha256Stream _caseTranscript;  // Incremental hash of Sigma1 || Sigma2 || Sigma3
    uint8_t     _caseInitiatorEphPub[kP256PubKeySize] = {};
};

} // namespace matter
