#include "MatterCrypto.h"
#include "MatterSession.h"
#include <string.h>

#ifdef NATIVE_TEST
// --- Stubs for native tests (no mbedTLS) ---
namespace matter {
bool pbkdf2Sha256(const uint8_t*, size_t, const uint8_t*, size_t, uint32_t, uint8_t*, size_t) { return false; }
bool spake2pDeriveW0W1(uint32_t, const uint8_t*, size_t, uint32_t, uint8_t*, uint8_t*) { return false; }
bool spake2pComputeResponse(Spake2pState&, const uint8_t*, size_t) { return false; }
bool spake2pVerify(const Spake2pState&, const uint8_t*, size_t) { return false; }
bool deriveSessionKeys(const uint8_t*, size_t, const uint8_t*, size_t, const char*, SessionKeys&) { return false; }
bool ecGenerateKeypair(ECKeyPair&) { return false; }
bool ecdhSharedSecret(const uint8_t*, const uint8_t*, uint8_t*, size_t&) { return false; }
bool ecdsaSign(const uint8_t*, const uint8_t*, size_t, uint8_t*, size_t&) { return false; }
bool ecdsaVerify(const uint8_t*, const uint8_t*, size_t, const uint8_t*, size_t) { return false; }
bool aesCcmEncrypt(const uint8_t*, const uint8_t*, size_t, const uint8_t*, size_t, const uint8_t*, size_t, uint8_t*, uint8_t*) { return false; }
bool aesCcmDecrypt(const uint8_t*, const uint8_t*, size_t, const uint8_t*, size_t, const uint8_t*, size_t, const uint8_t*, uint8_t*) { return false; }
bool hkdfSha256(const uint8_t*, size_t, const uint8_t*, size_t, const uint8_t*, size_t, uint8_t*, size_t) { return false; }
bool hmacSha256(const uint8_t*, size_t, const uint8_t*, size_t, uint8_t*) { return false; }
bool sha256(const uint8_t*, size_t, uint8_t*) { return false; }
void randomBytes(uint8_t* buf, size_t len) { memset(buf, 0xAA, len); }
bool generateCSR(const ECKeyPair&, uint8_t*, size_t&) { return false; }
bool compressedFabricId(const uint8_t*, uint64_t, uint8_t*) { return false; }
bool extractPubKeyFromCert(const uint8_t*, size_t, uint8_t*) { return false; }
bool extractIdsFromCert(const uint8_t*, size_t, uint64_t&, uint64_t&) { return false; }
bool FabricStore::saveCert(const char*, const uint8_t*, size_t) { return false; }
size_t FabricStore::loadCert(const char*, uint8_t*, size_t) { return 0; }
bool FabricStore::saveCore(const FabricInfo&) { return false; }
bool FabricStore::loadCore(FabricInfo&) { return false; }
bool FabricStore::erase() { return false; }
void Sha256Stream::begin() { if (_active) release(); _active = true; }
void Sha256Stream::update(const uint8_t*, size_t) {}
void Sha256Stream::snapshot(uint8_t* out) { memset(out, 0, 32); }
void Sha256Stream::finish(uint8_t* out) { memset(out, 0, 32); _active = false; }
void Sha256Stream::release() { _active = false; }
} // namespace matter

#else
// --- Real implementations using mbedTLS (ESP32) ---

#define MBEDTLS_ALLOW_PRIVATE_ACCESS
#include <mbedtls/ecp.h>
#include <mbedtls/bignum.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ccm.h>
#include <mbedtls/hkdf.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/sha256.h>
#include <mbedtls/md.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/x509_csr.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
#include <esp_random.h>

namespace matter {

// SPAKE2+ M and N points (uncompressed P-256, from Matter spec / RFC 9383)
static const uint8_t kPointM[65] = {
    0x04,
    0x88, 0x6e, 0x2f, 0x97, 0xac, 0xe4, 0x6e, 0x55,
    0xba, 0x9d, 0xd7, 0x24, 0x25, 0x79, 0xf2, 0x99,
    0x3b, 0x64, 0xe1, 0x6e, 0xf3, 0xdc, 0xab, 0x95,
    0xaf, 0xd4, 0x97, 0x33, 0x3d, 0x8f, 0xa1, 0x2f,
    0x5f, 0xf3, 0x55, 0x16, 0x3e, 0x43, 0xce, 0x22,
    0x4e, 0x0b, 0x0e, 0x65, 0xff, 0x02, 0xac, 0x8e,
    0x5c, 0x7b, 0xe0, 0x94, 0x19, 0xc7, 0x85, 0xe0,
    0xca, 0x54, 0x7d, 0x55, 0xa1, 0x2e, 0x2d, 0x20,
};
static const uint8_t kPointN[65] = {
    0x04,
    0xd8, 0xbb, 0xd6, 0xc6, 0x39, 0xc6, 0x29, 0x37,
    0xb0, 0x4d, 0x99, 0x7f, 0x38, 0xc3, 0x77, 0x07,
    0x19, 0xc6, 0x29, 0xd7, 0x01, 0x4d, 0x49, 0xa2,
    0x4b, 0x4f, 0x98, 0xba, 0xa1, 0x29, 0x2b, 0x49,
    0x07, 0xd6, 0x0a, 0xa6, 0xbf, 0xad, 0xe4, 0x50,
    0x08, 0xa6, 0x36, 0x33, 0x7f, 0x51, 0x68, 0xc6,
    0x4d, 0x9b, 0xd3, 0x60, 0x34, 0x80, 0x8c, 0xd5,
    0x64, 0x49, 0x0b, 0x1e, 0x65, 0x6e, 0xdb, 0xe7,
};

// ---------------------------------------------------------------------------
// DRBG helper (shared across calls within a single function scope)
// ---------------------------------------------------------------------------
struct RNG {
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    RNG() {
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&drbg);
        mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, nullptr, 0);
    }
    ~RNG() {
        mbedtls_ctr_drbg_free(&drbg);
        mbedtls_entropy_free(&entropy);
    }
    int operator()(void*, uint8_t* out, size_t len) {
        return mbedtls_ctr_drbg_random(&drbg, out, len);
    }
    static int cb(void* ctx, uint8_t* out, size_t len) {
        return static_cast<RNG*>(ctx)->operator()(nullptr, out, len);
    }
};

// ---------------------------------------------------------------------------
// Primitives
// ---------------------------------------------------------------------------

void randomBytes(uint8_t* buf, size_t len) {
    esp_fill_random(buf, len);
}

bool sha256(const uint8_t* data, size_t dataLen, uint8_t* out) {
    return mbedtls_sha256(data, dataLen, out, 0) == 0;
}

// ---------------------------------------------------------------------------
// Sha256Stream (incremental SHA-256 for CASE transcript)
// ---------------------------------------------------------------------------

static_assert(sizeof(mbedtls_sha256_context) <= 128, "Sha256Stream _state buffer too small");

void Sha256Stream::begin() {
    if (_active) release();
    auto* ctx = reinterpret_cast<mbedtls_sha256_context*>(_state);
    mbedtls_sha256_init(ctx);
    mbedtls_sha256_starts(ctx, 0);
    _active = true;
}

void Sha256Stream::update(const uint8_t* data, size_t len) {
    if (!_active || !data || len == 0) return;
    auto* ctx = reinterpret_cast<mbedtls_sha256_context*>(_state);
    mbedtls_sha256_update(ctx, data, len);
}

void Sha256Stream::snapshot(uint8_t* out) {
    if (!_active) { memset(out, 0, kSHA256Size); return; }
    auto* ctx = reinterpret_cast<mbedtls_sha256_context*>(_state);
    mbedtls_sha256_context clone;
    mbedtls_sha256_init(&clone);
    mbedtls_sha256_clone(&clone, ctx);
    mbedtls_sha256_finish(&clone, out);
    mbedtls_sha256_free(&clone);
}

void Sha256Stream::finish(uint8_t* out) {
    if (!_active) { memset(out, 0, kSHA256Size); return; }
    auto* ctx = reinterpret_cast<mbedtls_sha256_context*>(_state);
    mbedtls_sha256_finish(ctx, out);
    mbedtls_sha256_free(ctx);
    _active = false;
}

void Sha256Stream::release() {
    if (!_active) return;
    auto* ctx = reinterpret_cast<mbedtls_sha256_context*>(_state);
    mbedtls_sha256_free(ctx);
    _active = false;
}

bool hmacSha256(const uint8_t* key, size_t keyLen,
                const uint8_t* data, size_t dataLen, uint8_t* out) {
    const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return mbedtls_md_hmac(md, key, keyLen, data, dataLen, out) == 0;
}

bool hkdfSha256(const uint8_t* salt, size_t saltLen,
                const uint8_t* ikm, size_t ikmLen,
                const uint8_t* info, size_t infoLen,
                uint8_t* out, size_t outLen) {
    const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return mbedtls_hkdf(md, salt, saltLen, ikm, ikmLen, info, infoLen, out, outLen) == 0;
}

bool pbkdf2Sha256(const uint8_t* password, size_t passLen,
                  const uint8_t* salt, size_t saltLen,
                  uint32_t iterations,
                  uint8_t* output, size_t outLen) {
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (mbedtls_md_setup(&ctx, md, 1) != 0) { mbedtls_md_free(&ctx); return false; }
    int ret = mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, password, passLen,
                                             salt, saltLen, iterations, outLen, output);
    mbedtls_md_free(&ctx);
    return ret == 0;
}

// ---------------------------------------------------------------------------
// AES-128-CCM
// ---------------------------------------------------------------------------

bool aesCcmEncrypt(const uint8_t* key,
                   const uint8_t* nonce, size_t nonceLen,
                   const uint8_t* aad, size_t aadLen,
                   const uint8_t* plain, size_t plainLen,
                   uint8_t* cipher, uint8_t* tag) {
    mbedtls_ccm_context ctx;
    mbedtls_ccm_init(&ctx);
    if (mbedtls_ccm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 128) != 0) {
        mbedtls_ccm_free(&ctx); return false;
    }
    int ret = mbedtls_ccm_encrypt_and_tag(&ctx, plainLen, nonce, nonceLen,
                                           aad, aadLen, plain, cipher,
                                           tag, kCCMTagSize);
    mbedtls_ccm_free(&ctx);
    return ret == 0;
}

bool aesCcmDecrypt(const uint8_t* key,
                   const uint8_t* nonce, size_t nonceLen,
                   const uint8_t* aad, size_t aadLen,
                   const uint8_t* cipher, size_t cipherLen,
                   const uint8_t* tag,
                   uint8_t* plain) {
    mbedtls_ccm_context ctx;
    mbedtls_ccm_init(&ctx);
    if (mbedtls_ccm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 128) != 0) {
        mbedtls_ccm_free(&ctx); return false;
    }
    int ret = mbedtls_ccm_auth_decrypt(&ctx, cipherLen, nonce, nonceLen,
                                        aad, aadLen, cipher, plain,
                                        tag, kCCMTagSize);
    mbedtls_ccm_free(&ctx);
    return ret == 0;
}

// ---------------------------------------------------------------------------
// EC key generation + ECDH
// ---------------------------------------------------------------------------

bool ecGenerateKeypair(ECKeyPair& kp) {
    RNG rng;
    mbedtls_ecp_group grp;
    mbedtls_ecp_point Q;
    mbedtls_mpi d;
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&Q);
    mbedtls_mpi_init(&d);

    bool ok = false;
    if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
        mbedtls_ecp_gen_keypair(&grp, &d, &Q, RNG::cb, &rng) == 0) {
        size_t olen;
        mbedtls_ecp_point_write_binary(&grp, &Q, MBEDTLS_ECP_PF_UNCOMPRESSED,
                                        &olen, kp.pub, sizeof(kp.pub));
        mbedtls_mpi_write_binary(&d, kp.priv, sizeof(kp.priv));
        ok = true;
    }
    mbedtls_mpi_free(&d);
    mbedtls_ecp_point_free(&Q);
    mbedtls_ecp_group_free(&grp);
    return ok;
}

bool ecdhSharedSecret(const uint8_t* myPriv,
                      const uint8_t* peerPub,
                      uint8_t* secret, size_t& secretLen) {
    RNG rng;
    mbedtls_ecp_group grp;
    mbedtls_ecp_point Qp;
    mbedtls_mpi d, z;
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&Qp);
    mbedtls_mpi_init(&d);
    mbedtls_mpi_init(&z);

    bool ok = false;
    if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
        mbedtls_mpi_read_binary(&d, myPriv, kP256PrivKeySize) == 0 &&
        mbedtls_ecp_point_read_binary(&grp, &Qp, peerPub, kP256PubKeySize) == 0 &&
        mbedtls_ecdh_compute_shared(&grp, &z, &Qp, &d, RNG::cb, &rng) == 0) {
        secretLen = mbedtls_mpi_size(&z);
        mbedtls_mpi_write_binary(&z, secret, secretLen);
        ok = true;
    }
    mbedtls_mpi_free(&z);
    mbedtls_mpi_free(&d);
    mbedtls_ecp_point_free(&Qp);
    mbedtls_ecp_group_free(&grp);
    return ok;
}

// ---------------------------------------------------------------------------
// ECDSA (raw r||s, 64 bytes)
// ---------------------------------------------------------------------------

bool ecdsaSign(const uint8_t* privKey,
               const uint8_t* msg, size_t msgLen,
               uint8_t* sig, size_t& sigLen) {
    // Hash the message first
    uint8_t hash[kSHA256Size];
    if (!sha256(msg, msgLen, hash)) return false;

    RNG rng;
    mbedtls_mpi r, s, d;
    mbedtls_ecp_group grp;
    mbedtls_mpi_init(&r); mbedtls_mpi_init(&s); mbedtls_mpi_init(&d);
    mbedtls_ecp_group_init(&grp);

    bool ok = false;
    if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
        mbedtls_mpi_read_binary(&d, privKey, kP256PrivKeySize) == 0 &&
        mbedtls_ecdsa_sign_det_ext(&grp, &r, &s, &d, hash, kSHA256Size,
                                    MBEDTLS_MD_SHA256, RNG::cb, &rng) == 0) {
        mbedtls_mpi_write_binary(&r, sig, 32);
        mbedtls_mpi_write_binary(&s, sig + 32, 32);
        sigLen = 64;
        ok = true;
    }
    mbedtls_ecp_group_free(&grp);
    mbedtls_mpi_free(&d); mbedtls_mpi_free(&s); mbedtls_mpi_free(&r);
    return ok;
}

bool ecdsaVerify(const uint8_t* pubKey,
                 const uint8_t* msg, size_t msgLen,
                 const uint8_t* sig, size_t sigLen) {
    if (sigLen != 64) return false;
    uint8_t hash[kSHA256Size];
    if (!sha256(msg, msgLen, hash)) return false;

    mbedtls_mpi r, s;
    mbedtls_ecp_group grp;
    mbedtls_ecp_point Q;
    mbedtls_mpi_init(&r); mbedtls_mpi_init(&s);
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&Q);

    bool ok = false;
    if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
        mbedtls_ecp_point_read_binary(&grp, &Q, pubKey, kP256PubKeySize) == 0 &&
        mbedtls_mpi_read_binary(&r, sig, 32) == 0 &&
        mbedtls_mpi_read_binary(&s, sig + 32, 32) == 0 &&
        mbedtls_ecdsa_verify(&grp, hash, kSHA256Size, &Q, &r, &s) == 0) {
        ok = true;
    }
    mbedtls_ecp_point_free(&Q);
    mbedtls_ecp_group_free(&grp);
    mbedtls_mpi_free(&s); mbedtls_mpi_free(&r);
    return ok;
}

// ---------------------------------------------------------------------------
// SPAKE2+ (verifier side — device)
// ---------------------------------------------------------------------------

bool spake2pDeriveW0W1(uint32_t passcode,
                       const uint8_t* salt, size_t saltLen,
                       uint32_t iterations,
                       uint8_t* w0, uint8_t* w1) {
    // Encode passcode as 4-byte LE
    uint8_t pw[4] = {
        (uint8_t)(passcode & 0xFF),
        (uint8_t)((passcode >> 8) & 0xFF),
        (uint8_t)((passcode >> 16) & 0xFF),
        (uint8_t)((passcode >> 24) & 0xFF),
    };

    // Derive 80 bytes: w0s (40) || w1s (40)
    uint8_t ws[80];
    if (!pbkdf2Sha256(pw, sizeof(pw), salt, saltLen, iterations, ws, sizeof(ws)))
        return false;

    // Reduce w0s and w1s modulo the curve order
    mbedtls_ecp_group grp;
    mbedtls_mpi w0m, w1m, ws0, ws1;
    mbedtls_ecp_group_init(&grp);
    mbedtls_mpi_init(&w0m); mbedtls_mpi_init(&w1m);
    mbedtls_mpi_init(&ws0); mbedtls_mpi_init(&ws1);

    bool ok = false;
    if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
        mbedtls_mpi_read_binary(&ws0, ws, 40) == 0 &&
        mbedtls_mpi_read_binary(&ws1, ws + 40, 40) == 0 &&
        mbedtls_mpi_mod_mpi(&w0m, &ws0, &grp.N) == 0 &&
        mbedtls_mpi_mod_mpi(&w1m, &ws1, &grp.N) == 0) {
        mbedtls_mpi_write_binary(&w0m, w0, kP256PrivKeySize);
        mbedtls_mpi_write_binary(&w1m, w1, kP256PrivKeySize);
        ok = true;
    }
    mbedtls_mpi_free(&ws1); mbedtls_mpi_free(&ws0);
    mbedtls_mpi_free(&w1m); mbedtls_mpi_free(&w0m);
    mbedtls_ecp_group_free(&grp);
    return ok;
}

// Helper: append length (8-byte LE) + data to a SHA-256 context
static void ttAppend(mbedtls_sha256_context& ctx, const uint8_t* data, size_t len) {
    uint8_t lenBuf[8] = {};
    lenBuf[0] = len & 0xFF;
    lenBuf[1] = (len >> 8) & 0xFF;
    lenBuf[2] = (len >> 16) & 0xFF;
    lenBuf[3] = (len >> 24) & 0xFF;
    // bytes 4-7 stay 0 for any len < 4GB
    mbedtls_sha256_update(&ctx, lenBuf, 8);
    if (len > 0) mbedtls_sha256_update(&ctx, data, len);
}

bool spake2pComputeResponse(Spake2pState& st,
                            const uint8_t* context, size_t contextLen) {
    RNG rng;
    mbedtls_ecp_group grp;
    mbedtls_ecp_point M, N, Y, pBpt, pApt, X, Z, V, L;
    mbedtls_mpi y, w0m, w1m, one, neg_w0;

    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&M); mbedtls_ecp_point_init(&N);
    mbedtls_ecp_point_init(&Y); mbedtls_ecp_point_init(&pBpt);
    mbedtls_ecp_point_init(&pApt); mbedtls_ecp_point_init(&X);
    mbedtls_ecp_point_init(&Z); mbedtls_ecp_point_init(&V);
    mbedtls_ecp_point_init(&L);
    mbedtls_mpi_init(&y); mbedtls_mpi_init(&w0m); mbedtls_mpi_init(&w1m);
    mbedtls_mpi_init(&one); mbedtls_mpi_init(&neg_w0);

    bool ok = false;
    do {
        if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) != 0) break;
        if (mbedtls_ecp_point_read_binary(&grp, &M, kPointM, 65) != 0) break;
        if (mbedtls_ecp_point_read_binary(&grp, &N, kPointN, 65) != 0) break;
        if (mbedtls_mpi_read_binary(&w0m, st.w0, kP256PrivKeySize) != 0) break;
        if (mbedtls_mpi_read_binary(&w1m, st.w1, kP256PrivKeySize) != 0) break;
        if (mbedtls_ecp_point_read_binary(&grp, &pApt, st.pA, kP256PubKeySize) != 0) break;

        // y = random scalar
        if (mbedtls_ecp_gen_privkey(&grp, &y, RNG::cb, &rng) != 0) break;
        mbedtls_mpi_write_binary(&y, st.xy, kP256PrivKeySize);

        // Y = y * G
        if (mbedtls_ecp_mul(&grp, &Y, &y, &grp.G, RNG::cb, &rng) != 0) break;

        // pB = Y + w0 * N
        mbedtls_mpi_lset(&one, 1);
        if (mbedtls_ecp_muladd(&grp, &pBpt, &one, &Y, &w0m, &N) != 0) break;

        // Serialize pB
        size_t olen;
        if (mbedtls_ecp_point_write_binary(&grp, &pBpt, MBEDTLS_ECP_PF_UNCOMPRESSED,
                                            &olen, st.pB, kP256PubKeySize) != 0) break;

        // X = pA - w0*M  →  1*pA + (-w0)*M
        mbedtls_mpi_sub_mpi(&neg_w0, &grp.N, &w0m);
        if (mbedtls_ecp_muladd(&grp, &X, &one, &pApt, &neg_w0, &M) != 0) break;

        // Z = y * X
        if (mbedtls_ecp_mul(&grp, &Z, &y, &X, RNG::cb, &rng) != 0) break;

        // L = w1 * G  (precompute verifier's public key)
        if (mbedtls_ecp_mul(&grp, &L, &w1m, &grp.G, RNG::cb, &rng) != 0) break;

        // V = y * L  (NOT w1 * X — that gives w1*x*G instead of y*w1*G)
        if (mbedtls_ecp_mul(&grp, &V, &y, &L, RNG::cb, &rng) != 0) break;

        // Serialize Z and V
        uint8_t Zbuf[65], Vbuf[65];
        mbedtls_ecp_point_write_binary(&grp, &Z, MBEDTLS_ECP_PF_UNCOMPRESSED, &olen, Zbuf, 65);
        mbedtls_ecp_point_write_binary(&grp, &V, MBEDTLS_ECP_PF_UNCOMPRESSED, &olen, Vbuf, 65);

        // Serialize w0
        uint8_t w0buf[kP256PrivKeySize];
        memcpy(w0buf, st.w0, kP256PrivKeySize);

        // Build transcript hash TT (per SPAKE2+-P256-SHA256-HKDF draft-01)
        //   TT = Hash( context || len(A)||A || len(B)||B ||
        //              len(M)||M || len(N)||N ||
        //              len(X)||X || len(Y)||Y ||
        //              len(Z)||Z || len(V)||V || len(w0)||w0 )
        // Where X=pA (prover share), Y=pB (verifier share)
        // Context is already a 32-byte hash from Matter PASE layer
        mbedtls_sha256_context sha;
        mbedtls_sha256_init(&sha);
        mbedtls_sha256_starts(&sha, 0);

        // Context (32-byte hash from PASE layer)
        ttAppend(sha, context, contextLen);

        // Empty identities (Matter PASE uses nil for both)
        ttAppend(sha, nullptr, 0);
        ttAppend(sha, nullptr, 0);

        // M and N points (65 bytes each)
        ttAppend(sha, kPointM, 65);
        ttAppend(sha, kPointN, 65);

        // X = pA (prover's share), Y = pB (verifier's share)
        ttAppend(sha, st.pA, kP256PubKeySize);
        ttAppend(sha, st.pB, kP256PubKeySize);

        // Z, V (65 bytes each)
        ttAppend(sha, Zbuf, 65);
        ttAppend(sha, Vbuf, 65);

        // w0 (32 bytes)
        ttAppend(sha, w0buf, kP256PrivKeySize);

        uint8_t hashTT[kSHA256Size];
        mbedtls_sha256_finish(&sha, hashTT);
        mbedtls_sha256_free(&sha);

        // Ka || Ke = hashTT split in half (NO HKDF here — per draft-01)
        uint8_t Ka[kAES128KeySize];
        memcpy(Ka, hashTT, kAES128KeySize);         // Ka = first 16 bytes
        memcpy(st.Ke, hashTT + kAES128KeySize, kAES128KeySize); // Ke = next 16 bytes

        // KcA || KcB = HKDF(salt=nil, IKM=Ka, info="ConfirmationKeys", L=32)
        uint8_t KcAKcB[32];
        if (!hkdfSha256(nullptr, 0, Ka, kAES128KeySize,
                        (const uint8_t*)"ConfirmationKeys", 16, KcAKcB, 32)) break;
        memcpy(st.KcA, KcAKcB, 16);
        memcpy(st.KcB, KcAKcB + 16, 16);

        // cB = HMAC(KcB, pA) — verifier MACs the *prover's* share
        if (!hmacSha256(st.KcB, 16, st.pA, kP256PubKeySize, st.cB)) break;

        ok = true;
    } while (false);

    mbedtls_mpi_free(&neg_w0); mbedtls_mpi_free(&one);
    mbedtls_mpi_free(&w1m); mbedtls_mpi_free(&w0m); mbedtls_mpi_free(&y);
    mbedtls_ecp_point_free(&L);
    mbedtls_ecp_point_free(&V); mbedtls_ecp_point_free(&Z);
    mbedtls_ecp_point_free(&X); mbedtls_ecp_point_free(&pApt);
    mbedtls_ecp_point_free(&pBpt); mbedtls_ecp_point_free(&Y);
    mbedtls_ecp_point_free(&N); mbedtls_ecp_point_free(&M);
    mbedtls_ecp_group_free(&grp);
    return ok;
}

bool spake2pVerify(const Spake2pState& st, const uint8_t* cA, size_t cALen) {
    if (cALen != kHMACSize) return false;
    // Expected: cA = HMAC(KcA, pB) — prover MACs the *verifier's* share
    uint8_t expected[kHMACSize];
    if (!hmacSha256(st.KcA, 16, st.pB, kP256PubKeySize, expected)) return false;
    // Constant-time compare
    uint8_t diff = 0;
    for (size_t i = 0; i < kHMACSize; i++) diff |= expected[i] ^ cA[i];
    return diff == 0;
}

// ---------------------------------------------------------------------------
// Session key derivation
// ---------------------------------------------------------------------------

bool deriveSessionKeys(const uint8_t* secret, size_t secretLen,
                       const uint8_t* salt, size_t saltLen,
                       const char* info, SessionKeys& keys) {
    uint8_t keyMaterial[48]; // I2R(16) + R2I(16) + AttestChallenge(16)
    if (!hkdfSha256(salt, saltLen, secret, secretLen,
                    (const uint8_t*)info, strlen(info),
                    keyMaterial, 48)) return false;
    memcpy(keys.i2rKey, keyMaterial, 16);
    memcpy(keys.r2iKey, keyMaterial + 16, 16);
    memcpy(keys.attestChallenge, keyMaterial + 32, 16);
    return true;
}

// ---------------------------------------------------------------------------
// CSR generation
// ---------------------------------------------------------------------------

bool generateCSR(const ECKeyPair& kp, uint8_t* csrDer, size_t& csrLen) {
    RNG rng;
    mbedtls_x509write_csr csr;
    mbedtls_pk_context pk;

    mbedtls_x509write_csr_init(&csr);
    mbedtls_pk_init(&pk);

    bool ok = false;
    do {
        if (mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) != 0) break;
        mbedtls_ecp_keypair* ec = mbedtls_pk_ec(pk);
        if (mbedtls_ecp_group_load(&ec->grp, MBEDTLS_ECP_DP_SECP256R1) != 0) break;
        if (mbedtls_mpi_read_binary(&ec->d, kp.priv, kP256PrivKeySize) != 0) break;
        if (mbedtls_ecp_point_read_binary(&ec->grp, &ec->Q, kp.pub, kP256PubKeySize) != 0) break;

        mbedtls_x509write_csr_set_key(&csr, &pk);
        mbedtls_x509write_csr_set_subject_name(&csr, "O=CSA,CN=Matter Device");
        mbedtls_x509write_csr_set_md_alg(&csr, MBEDTLS_MD_SHA256);

        // mbedtls writes DER from the end of the buffer
        int ret = mbedtls_x509write_csr_der(&csr, csrDer, kMaxCSRSize,
                                             RNG::cb, &rng);
        if (ret <= 0) break;

        // Move to start of buffer
        memmove(csrDer, csrDer + kMaxCSRSize - ret, ret);
        csrLen = (size_t)ret;
        ok = true;
    } while (false);

    mbedtls_pk_free(&pk);
    mbedtls_x509write_csr_free(&csr);
    return ok;
}

// ---------------------------------------------------------------------------
// Extract public key from DER-encoded X.509 certificate
// ---------------------------------------------------------------------------

bool extractPubKeyFromCert(const uint8_t* certDer, size_t certLen,
                           uint8_t* pubKey) {
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);

    bool ok = false;
    if (mbedtls_x509_crt_parse_der(&crt, certDer, certLen) == 0) {
        if (mbedtls_pk_get_type(&crt.pk) == MBEDTLS_PK_ECKEY) {
            mbedtls_ecp_keypair* ec = mbedtls_pk_ec(crt.pk);
            size_t olen;
            if (mbedtls_ecp_point_write_binary(&ec->grp, &ec->Q,
                    MBEDTLS_ECP_PF_UNCOMPRESSED, &olen,
                    pubKey, kP256PubKeySize) == 0 && olen == kP256PubKeySize) {
                ok = true;
            }
        }
    }
    mbedtls_x509_crt_free(&crt);
    return ok;
}

// ---------------------------------------------------------------------------
// Extract Matter node ID and fabric ID from NOC subject DN
// Matter OIDs: 1.3.6.1.4.1.37244.1.1 (node-id), 1.3.6.1.4.1.37244.1.5 (fabric-id)
// OID bytes: 06 0A 2B 06 01 04 01 82 A2 7C 01 01 (node-id)
//            06 0A 2B 06 01 04 01 82 A2 7C 01 05 (fabric-id)
// ---------------------------------------------------------------------------

static const uint8_t kOidMatterNodeId[] = {
    0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0xA2, 0x7C, 0x01, 0x01
};
static const uint8_t kOidMatterFabricId[] = {
    0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0xA2, 0x7C, 0x01, 0x05
};

// Search for an OID in DER data and extract the following INTEGER value as uint64
static bool findOidValue(const uint8_t* der, size_t derLen,
                         const uint8_t* oid, size_t oidLen,
                         uint64_t& value) {
    // Brute-force search for the OID byte pattern in the DER
    for (size_t i = 0; i + oidLen + 2 < derLen; i++) {
        // Look for OID tag (0x06) followed by length matching oidLen
        if (der[i] == 0x06 && der[i + 1] == oidLen &&
            memcmp(der + i + 2, oid, oidLen) == 0) {
            // The value follows: expect a UTF8String or PrintableString tag
            size_t vpos = i + 2 + oidLen;
            if (vpos + 2 > derLen) return false;
            uint8_t vtag = der[vpos++];
            (void)vtag; // Tag type varies (UTF8String=0x0C, PrintableString=0x13, etc.)
            uint8_t vlen = der[vpos++];
            if (vpos + vlen > derLen || vlen > 16) return false;
            // Parse hex-encoded node/fabric ID string, or raw integer
            // Matter NOCs encode these as ASN.1 UTF8String containing hex
            // Parse as big-endian hex string
            value = 0;
            for (size_t j = 0; j < vlen; j++) {
                uint8_t c = der[vpos + j];
                uint8_t nibble;
                if (c >= '0' && c <= '9') nibble = c - '0';
                else if (c >= 'A' && c <= 'F') nibble = c - 'A' + 10;
                else if (c >= 'a' && c <= 'f') nibble = c - 'a' + 10;
                else return false;
                value = (value << 4) | nibble;
            }
            return true;
        }
    }
    return false;
}

} // namespace matter (close before system includes)

// ---------------------------------------------------------------------------
// FabricStore — NVS persistence for fabric data
// ---------------------------------------------------------------------------

#include <Preferences.h>

namespace matter { // reopen

static const char* kNvsNamespace = "matter_fab";

bool FabricStore::saveCert(const char* key, const uint8_t* data, size_t len) {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, false)) return false;
    size_t written = prefs.putBytes(key, data, len);
    prefs.end();
    return written == len;
}

size_t FabricStore::loadCert(const char* key, uint8_t* buf, size_t bufSize) {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, true)) return 0;
    size_t len = prefs.getBytes(key, buf, bufSize);
    prefs.end();
    return len;
}

bool FabricStore::saveCore(const FabricInfo& fab) {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, false)) return false;
    prefs.putBool("valid", fab.valid);
    prefs.putBytes("oppriv", fab.operationalKey.priv, kP256PrivKeySize);
    prefs.putBytes("oppub", fab.operationalKey.pub, kP256PubKeySize);
    prefs.putBytes("ipk", fab.ipk, 16);
    prefs.putULong64("nodeid", fab.nodeId);
    prefs.putULong64("fabid", fab.fabricId);
    prefs.end();
    return true;
}

bool FabricStore::loadCore(FabricInfo& fab) {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, true)) return false;
    fab.valid = prefs.getBool("valid", false);
    if (!fab.valid) { prefs.end(); return false; }
    prefs.getBytes("oppriv", fab.operationalKey.priv, kP256PrivKeySize);
    prefs.getBytes("oppub", fab.operationalKey.pub, kP256PubKeySize);
    prefs.getBytes("ipk", fab.ipk, 16);
    fab.nodeId = prefs.getULong64("nodeid", 0);
    fab.fabricId = prefs.getULong64("fabid", 0);
    prefs.end();
    return true;
}

bool FabricStore::erase() {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, false)) return false;
    prefs.clear();
    prefs.end();
    return true;
}

// ---------------------------------------------------------------------------
// Extract Matter node ID and fabric ID from NOC subject DN
// ---------------------------------------------------------------------------

bool extractIdsFromCert(const uint8_t* certDer, size_t certLen,
                        uint64_t& nodeId, uint64_t& fabricId) {
    nodeId = 0;
    fabricId = 0;
    bool gotNode = findOidValue(certDer, certLen, kOidMatterNodeId,
                                sizeof(kOidMatterNodeId), nodeId);
    bool gotFabric = findOidValue(certDer, certLen, kOidMatterFabricId,
                                  sizeof(kOidMatterFabricId), fabricId);
    return gotNode || gotFabric;
}

// ---------------------------------------------------------------------------
// Compressed Fabric Identifier (Spec 4.3.2.2)
// HKDF-SHA256(salt=rootPubKey(65), ikm=fabricId(8 BE), info="CompressedFabric", L=8)
// ---------------------------------------------------------------------------

bool compressedFabricId(const uint8_t* rootPubKey,
                        uint64_t fabricId,
                        uint8_t* out8) {
    uint8_t fabricIdBE[8];
    for (int i = 0; i < 8; i++)
        fabricIdBE[i] = (fabricId >> (56 - i * 8)) & 0xFF;

    uint8_t full[8];
    if (!hkdfSha256(rootPubKey, kP256PubKeySize,
                    fabricIdBE, 8,
                    (const uint8_t*)"CompressedFabric", 16,
                    full, 8)) return false;
    memcpy(out8, full, 8);
    return true;
}

} // namespace matter
#endif // NATIVE_TEST
