#pragma once
#include <stdint.h>
#include <stddef.h>
#include "MatterConst.h"

namespace matter {

// ---------------------------------------------------------------------------
// Key types
// ---------------------------------------------------------------------------
struct ECKeyPair {
    uint8_t pub[kP256PubKeySize];
    uint8_t priv[kP256PrivKeySize];
};

struct SessionKeys {
    uint8_t i2rKey[kAES128KeySize];
    uint8_t r2iKey[kAES128KeySize];
    uint8_t attestChallenge[kAES128KeySize];
};

// ---------------------------------------------------------------------------
// Incremental SHA-256 (for CASE transcript hashing)
// ---------------------------------------------------------------------------
struct Sha256Stream {
    uint8_t _state[128] = {};  // Opaque storage for mbedtls_sha256_context
    bool _active = false;

    void begin();
    void update(const uint8_t* data, size_t len);
    void snapshot(uint8_t* out);   // Non-destructive: clones internal state
    void finish(uint8_t* out);     // Destructive: finalizes and frees
    void release();                // Free without finalizing
};

// ---------------------------------------------------------------------------
// PBKDF2
// ---------------------------------------------------------------------------
bool pbkdf2Sha256(const uint8_t* password, size_t passLen,
                  const uint8_t* salt, size_t saltLen,
                  uint32_t iterations,
                  uint8_t* output, size_t outLen);

// ---------------------------------------------------------------------------
// SPAKE2+ (device = verifier)
// ---------------------------------------------------------------------------

// Holds state across PASE rounds.  Allocated on stack during commissioning,
// so we size it to hold the intermediate scalars and points.
struct Spake2pState {
    uint8_t w0[kP256PrivKeySize];
    uint8_t w1[kP256PrivKeySize];
    uint8_t xy[kP256PrivKeySize];      // our random scalar
    uint8_t pB[kP256PubKeySize];       // our serialized point
    uint8_t pA[kP256PubKeySize];       // peer's serialized point
    uint8_t Ke[kAES128KeySize];        // encryption key (hash_size/2 = 16 bytes)
    uint8_t KcA[kAES128KeySize];       // peer's confirmation key
    uint8_t KcB[kAES128KeySize];       // our confirmation key
    uint8_t cB[kHMACSize];             // our confirmation value
};

// Derive w0,w1 from passcode
bool spake2pDeriveW0W1(uint32_t passcode,
                       const uint8_t* salt, size_t saltLen,
                       uint32_t iterations,
                       uint8_t* w0, uint8_t* w1);

// Generate pB and cB given pA from the commissioner.
// context = raw bytes of PBKDFParamRequest || PBKDFParamResponse
bool spake2pComputeResponse(Spake2pState& st,
                            const uint8_t* context, size_t contextLen);

// Verify cA received in Pake3
bool spake2pVerify(const Spake2pState& st, const uint8_t* cA, size_t cALen);

// ---------------------------------------------------------------------------
// Session key derivation (from SPAKE2+ Ke or CASE shared secret)
// ---------------------------------------------------------------------------
bool deriveSessionKeys(const uint8_t* secret, size_t secretLen,
                       const uint8_t* salt, size_t saltLen,
                       const char* info, SessionKeys& keys);

// ---------------------------------------------------------------------------
// ECDHE (P-256)
// ---------------------------------------------------------------------------
bool ecGenerateKeypair(ECKeyPair& kp);
bool ecdhSharedSecret(const uint8_t* myPriv,
                      const uint8_t* peerPub,
                      uint8_t* secret, size_t& secretLen);

// ---------------------------------------------------------------------------
// ECDSA-P256-SHA256
// ---------------------------------------------------------------------------
bool ecdsaSign(const uint8_t* privKey,
               const uint8_t* msg, size_t msgLen,
               uint8_t* sig, size_t& sigLen);

bool ecdsaVerify(const uint8_t* pubKey,
                 const uint8_t* msg, size_t msgLen,
                 const uint8_t* sig, size_t sigLen);

// ---------------------------------------------------------------------------
// AES-128-CCM
// ---------------------------------------------------------------------------
bool aesCcmEncrypt(const uint8_t* key,
                   const uint8_t* nonce, size_t nonceLen,
                   const uint8_t* aad, size_t aadLen,
                   const uint8_t* plain, size_t plainLen,
                   uint8_t* cipher, uint8_t* tag);

bool aesCcmDecrypt(const uint8_t* key,
                   const uint8_t* nonce, size_t nonceLen,
                   const uint8_t* aad, size_t aadLen,
                   const uint8_t* cipher, size_t cipherLen,
                   const uint8_t* tag,
                   uint8_t* plain);

// ---------------------------------------------------------------------------
// Primitives
// ---------------------------------------------------------------------------
bool hkdfSha256(const uint8_t* salt, size_t saltLen,
                const uint8_t* ikm, size_t ikmLen,
                const uint8_t* info, size_t infoLen,
                uint8_t* out, size_t outLen);

bool hmacSha256(const uint8_t* key, size_t keyLen,
                const uint8_t* data, size_t dataLen,
                uint8_t* out);

bool sha256(const uint8_t* data, size_t dataLen, uint8_t* out);

void randomBytes(uint8_t* buf, size_t len);

// ---------------------------------------------------------------------------
// Compressed Fabric Identifier (Spec 4.3.2.2)
// ---------------------------------------------------------------------------
// HKDF-SHA256(salt=rootPubKey(65), ikm=fabricId(8, BE), info="CompressedFabric", L=8)
bool compressedFabricId(const uint8_t* rootPubKey,
                        uint64_t fabricId,
                        uint8_t* out8);

// ---------------------------------------------------------------------------
// CSR generation (PKCS#10)
// ---------------------------------------------------------------------------
bool generateCSR(const ECKeyPair& kp, uint8_t* csrDer, size_t& csrLen);

// ---------------------------------------------------------------------------
// X.509 certificate utilities
// ---------------------------------------------------------------------------
// Extract uncompressed P-256 public key (65 bytes) from a DER-encoded certificate
bool extractPubKeyFromCert(const uint8_t* certDer, size_t certLen,
                           uint8_t* pubKey);

// Extract Matter node ID and fabric ID from a DER-encoded NOC certificate
// Looks for Matter-specific OIDs in the subject DN:
//   matter-node-id:   1.3.6.1.4.1.37244.1.1
//   matter-fabric-id: 1.3.6.1.4.1.37244.1.5
bool extractIdsFromCert(const uint8_t* certDer, size_t certLen,
                        uint64_t& nodeId, uint64_t& fabricId);

} // namespace matter
