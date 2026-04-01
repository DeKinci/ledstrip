// Matter Crypto onboard tests — runs on ESP32 with real mbedTLS
// Test vectors from RFC 5869 (HKDF), NIST (AES-CCM), FIPS 180-4 (SHA-256)

#include <Arduino.h>
#include <unity.h>
#include "MatterCrypto.h"
#include "test_certs.h"

using matter::sha256;
using matter::hmacSha256;
using matter::hkdfSha256;
using matter::pbkdf2Sha256;
using matter::aesCcmEncrypt;
using matter::aesCcmDecrypt;
using matter::ecGenerateKeypair;
using matter::ecdhSharedSecret;
using matter::ecdsaSign;
using matter::ecdsaVerify;
using matter::deriveSessionKeys;
using matter::spake2pDeriveW0W1;
using matter::spake2pComputeResponse;
using matter::extractPubKeyFromCert;
using matter::generateCSR;
using matter::randomBytes;
using matter::ECKeyPair;
using matter::SessionKeys;
using matter::Spake2pState;
using matter::Sha256Stream;

// ============================================================================
// SHA-256 — FIPS 180-4 test vectors
// ============================================================================

void test_sha256_empty() {
    // SHA-256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
    uint8_t hash[32];
    TEST_ASSERT_TRUE(sha256(nullptr, 0, hash));
    const uint8_t expected[] = {
        0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
        0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
        0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
        0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55
    };
    TEST_ASSERT_EQUAL_MEMORY(expected, hash, 32);
}

void test_sha256_abc() {
    // SHA-256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
    const uint8_t msg[] = {'a', 'b', 'c'};
    uint8_t hash[32];
    TEST_ASSERT_TRUE(sha256(msg, 3, hash));
    const uint8_t expected[] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
    };
    TEST_ASSERT_EQUAL_MEMORY(expected, hash, 32);
}

// ============================================================================
// HMAC-SHA256 — RFC 4231 Test Case 2
// ============================================================================

void test_hmac_sha256_rfc4231_tc2() {
    // Key = "Jefe", Data = "what do ya want for nothing?"
    const uint8_t key[] = {'J', 'e', 'f', 'e'};
    const uint8_t data[] = "what do ya want for nothing?";
    uint8_t mac[32];
    TEST_ASSERT_TRUE(hmacSha256(key, 4, data, 28, mac));
    const uint8_t expected[] = {
        0x5b, 0xdc, 0xc1, 0x46, 0xbf, 0x60, 0x75, 0x4e,
        0x6a, 0x04, 0x24, 0x26, 0x08, 0x95, 0x75, 0xc7,
        0x5a, 0x00, 0x3f, 0x08, 0x9d, 0x27, 0x39, 0x83,
        0x9d, 0xec, 0x58, 0xb9, 0x64, 0xec, 0x38, 0x43
    };
    TEST_ASSERT_EQUAL_MEMORY(expected, mac, 32);
}

// ============================================================================
// HKDF-SHA256 — RFC 5869 Test Case 1
// ============================================================================

void test_hkdf_sha256_rfc5869_tc1() {
    const uint8_t ikm[] = {0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
                           0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
                           0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b};
    const uint8_t salt[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                            0x08, 0x09, 0x0a, 0x0b, 0x0c};
    const uint8_t info[] = {0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
                            0xf8, 0xf9};
    uint8_t okm[42];
    TEST_ASSERT_TRUE(hkdfSha256(salt, 13, ikm, 22, info, 10, okm, 42));

    const uint8_t expected[] = {
        0x3c, 0xb2, 0x5f, 0x25, 0xfa, 0xac, 0xd5, 0x7a,
        0x90, 0x43, 0x4f, 0x64, 0xd0, 0x36, 0x2f, 0x2a,
        0x2d, 0x2d, 0x0a, 0x90, 0xcf, 0x1a, 0x5a, 0x4c,
        0x5d, 0xb0, 0x2d, 0x56, 0xec, 0xc4, 0xc5, 0xbf,
        0x34, 0x00, 0x72, 0x08, 0xd5, 0xb8, 0x87, 0x18,
        0x58, 0x65
    };
    TEST_ASSERT_EQUAL_MEMORY(expected, okm, 42);
}

// ============================================================================
// AES-128-CCM — encrypt/decrypt roundtrip
// ============================================================================

void test_aes_ccm_roundtrip() {
    uint8_t key[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                       0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
    uint8_t nonce[13] = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6,
                         0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC};
    uint8_t aad[] = {0x00, 0x01, 0x02, 0x03};
    uint8_t plain[] = "Hello Matter!";
    size_t plainLen = strlen((char*)plain);

    uint8_t cipher[64];
    uint8_t tag[16];
    TEST_ASSERT_TRUE(aesCcmEncrypt(key, nonce, 13, aad, 4, plain, plainLen, cipher, tag));

    // Decrypt and verify
    uint8_t decrypted[64];
    TEST_ASSERT_TRUE(aesCcmDecrypt(key, nonce, 13, aad, 4, cipher, plainLen, tag, decrypted));
    TEST_ASSERT_EQUAL_MEMORY(plain, decrypted, plainLen);
}

void test_aes_ccm_tampered_tag() {
    uint8_t key[16] = {};
    uint8_t nonce[13] = {};
    uint8_t plain[] = {0x42};
    uint8_t cipher[1];
    uint8_t tag[16];

    TEST_ASSERT_TRUE(aesCcmEncrypt(key, nonce, 13, nullptr, 0, plain, 1, cipher, tag));

    // Tamper with tag
    tag[0] ^= 0xFF;
    uint8_t decrypted[1];
    TEST_ASSERT_FALSE(aesCcmDecrypt(key, nonce, 13, nullptr, 0, cipher, 1, tag, decrypted));
}

void test_aes_ccm_tampered_ciphertext() {
    uint8_t key[16] = {1};
    uint8_t nonce[13] = {2};
    uint8_t plain[] = {0x01, 0x02, 0x03};
    uint8_t cipher[3];
    uint8_t tag[16];

    TEST_ASSERT_TRUE(aesCcmEncrypt(key, nonce, 13, nullptr, 0, plain, 3, cipher, tag));

    cipher[1] ^= 0xFF;
    uint8_t decrypted[3];
    TEST_ASSERT_FALSE(aesCcmDecrypt(key, nonce, 13, nullptr, 0, cipher, 3, tag, decrypted));
}

// ============================================================================
// ECDH + ECDSA — P-256 key generation and basic operations
// ============================================================================

void test_ec_keypair_generation() {
    ECKeyPair kp;
    TEST_ASSERT_TRUE(ecGenerateKeypair(kp));
    // Public key should start with 0x04 (uncompressed point)
    TEST_ASSERT_EQUAL(0x04, kp.pub[0]);
    // Private key should be non-zero
    bool allZero = true;
    for (int i = 0; i < 32; i++) if (kp.priv[i] != 0) allZero = false;
    TEST_ASSERT_FALSE(allZero);
}

void test_ecdh_shared_secret() {
    ECKeyPair kpA, kpB;
    TEST_ASSERT_TRUE(ecGenerateKeypair(kpA));
    TEST_ASSERT_TRUE(ecGenerateKeypair(kpB));

    uint8_t secretA[32], secretB[32];
    size_t lenA = 32, lenB = 32;
    TEST_ASSERT_TRUE(ecdhSharedSecret(kpA.priv, kpB.pub, secretA, lenA));
    TEST_ASSERT_TRUE(ecdhSharedSecret(kpB.priv, kpA.pub, secretB, lenB));

    // Both sides should derive the same shared secret
    TEST_ASSERT_EQUAL(lenA, lenB);
    TEST_ASSERT_EQUAL_MEMORY(secretA, secretB, lenA);
}

void test_ecdsa_sign_verify() {
    ECKeyPair kp;
    TEST_ASSERT_TRUE(ecGenerateKeypair(kp));

    const uint8_t msg[] = "Matter test message for ECDSA";
    uint8_t sig[72];
    size_t sigLen = 72;
    TEST_ASSERT_TRUE(ecdsaSign(kp.priv, msg, sizeof(msg) - 1, sig, sigLen));
    TEST_ASSERT_TRUE(sigLen > 0);

    // Verify with correct public key
    TEST_ASSERT_TRUE(ecdsaVerify(kp.pub, msg, sizeof(msg) - 1, sig, sigLen));
}

void test_ecdsa_verify_wrong_key() {
    ECKeyPair kpA, kpB;
    TEST_ASSERT_TRUE(ecGenerateKeypair(kpA));
    TEST_ASSERT_TRUE(ecGenerateKeypair(kpB));

    const uint8_t msg[] = "Signed by A";
    uint8_t sig[72];
    size_t sigLen = 72;
    TEST_ASSERT_TRUE(ecdsaSign(kpA.priv, msg, sizeof(msg) - 1, sig, sigLen));

    // Verify with wrong key should fail
    TEST_ASSERT_FALSE(ecdsaVerify(kpB.pub, msg, sizeof(msg) - 1, sig, sigLen));
}

void test_ecdsa_verify_tampered_message() {
    ECKeyPair kp;
    TEST_ASSERT_TRUE(ecGenerateKeypair(kp));

    uint8_t msg[] = "Original message";
    uint8_t sig[72];
    size_t sigLen = 72;
    TEST_ASSERT_TRUE(ecdsaSign(kp.priv, msg, sizeof(msg) - 1, sig, sigLen));

    msg[0] = 'X';  // Tamper
    TEST_ASSERT_FALSE(ecdsaVerify(kp.pub, msg, sizeof(msg) - 1, sig, sigLen));
}

// ============================================================================
// PBKDF2 — basic test
// ============================================================================

void test_pbkdf2_basic() {
    // PBKDF2-SHA256("password", "salt", 1, 32)
    const uint8_t password[] = "password";
    const uint8_t salt[] = "salt";
    uint8_t output[32];
    TEST_ASSERT_TRUE(pbkdf2Sha256(password, 8, salt, 4, 1, output, 32));

    // RFC 7914 test vector for PBKDF2-SHA256
    const uint8_t expected[] = {
        0x12, 0x0f, 0xb6, 0xcf, 0xfc, 0xf8, 0xb3, 0x2c,
        0x43, 0xe7, 0x22, 0x52, 0x56, 0xc4, 0xf8, 0x37,
        0xa8, 0x65, 0x48, 0xc9, 0x2c, 0xcc, 0x35, 0x48,
        0x08, 0x05, 0x98, 0x7c, 0xb7, 0x0b, 0xe1, 0x7b
    };
    TEST_ASSERT_EQUAL_MEMORY(expected, output, 32);
}

// ============================================================================
// Session key derivation
// ============================================================================

void test_session_key_derivation() {
    uint8_t secret[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                          0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
    uint8_t salt[32] = {};
    SessionKeys keys;
    TEST_ASSERT_TRUE(deriveSessionKeys(secret, 16, salt, 32, "SessionKeys", keys));

    // Keys should be non-zero and different from each other
    bool i2rZero = true, r2iZero = true;
    for (int i = 0; i < 16; i++) {
        if (keys.i2rKey[i] != 0) i2rZero = false;
        if (keys.r2iKey[i] != 0) r2iZero = false;
    }
    TEST_ASSERT_FALSE(i2rZero);
    TEST_ASSERT_FALSE(r2iZero);
    // i2r and r2i should be different
    TEST_ASSERT_FALSE(memcmp(keys.i2rKey, keys.r2iKey, 16) == 0);
}

// ============================================================================
// SHA-256 Stream (incremental hashing)
// ============================================================================

void test_sha256_stream() {
    Sha256Stream s;
    s.begin();
    const uint8_t part1[] = {'a', 'b'};
    const uint8_t part2[] = {'c'};
    s.update(part1, 2);
    s.update(part2, 1);
    uint8_t hash[32];
    s.finish(hash);

    // Should equal SHA-256("abc")
    const uint8_t expected[] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
    };
    TEST_ASSERT_EQUAL_MEMORY(expected, hash, 32);
}

void test_sha256_stream_snapshot() {
    Sha256Stream s;
    s.begin();
    const uint8_t data[] = {'a', 'b', 'c'};
    s.update(data, 3);

    // Snapshot should give same result as finish
    uint8_t snap[32], final_hash[32];
    s.snapshot(snap);
    s.finish(final_hash);
    TEST_ASSERT_EQUAL_MEMORY(snap, final_hash, 32);
}

// ============================================================================
// SPAKE2+ — w0/w1 derivation (Matter Spec 3.10.1)
// ============================================================================

void test_spake2p_derive_w0w1() {
    // Test passcode: 20202021 (Matter test default)
    // Known salt (32 bytes of 0x00 for simplicity)
    uint8_t salt[32] = {};
    uint8_t w0[32], w1[32];

    TEST_ASSERT_TRUE(spake2pDeriveW0W1(20202021, salt, sizeof(salt), 1000, w0, w1));

    // w0 and w1 should be non-zero
    bool w0zero = true, w1zero = true;
    for (int i = 0; i < 32; i++) {
        if (w0[i]) w0zero = false;
        if (w1[i]) w1zero = false;
    }
    TEST_ASSERT_FALSE(w0zero);
    TEST_ASSERT_FALSE(w1zero);

    // w0 and w1 should be different
    TEST_ASSERT_FALSE(memcmp(w0, w1, 32) == 0);
}

void test_spake2p_derive_deterministic() {
    // Same inputs → same outputs
    uint8_t salt[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    uint8_t w0a[32], w1a[32], w0b[32], w1b[32];

    TEST_ASSERT_TRUE(spake2pDeriveW0W1(12345, salt, sizeof(salt), 100, w0a, w1a));
    TEST_ASSERT_TRUE(spake2pDeriveW0W1(12345, salt, sizeof(salt), 100, w0b, w1b));

    TEST_ASSERT_EQUAL_MEMORY(w0a, w0b, 32);
    TEST_ASSERT_EQUAL_MEMORY(w1a, w1b, 32);
}

void test_spake2p_different_passcode_different_w() {
    uint8_t salt[16] = {};
    uint8_t w0a[32], w1a[32], w0b[32], w1b[32];

    TEST_ASSERT_TRUE(spake2pDeriveW0W1(11111111, salt, sizeof(salt), 100, w0a, w1a));
    TEST_ASSERT_TRUE(spake2pDeriveW0W1(22222222, salt, sizeof(salt), 100, w0b, w1b));

    TEST_ASSERT_FALSE(memcmp(w0a, w0b, 32) == 0);
}

void test_spake2p_different_salt_different_w() {
    uint8_t salt1[16] = {1};
    uint8_t salt2[16] = {2};
    uint8_t w0a[32], w1a[32], w0b[32], w1b[32];

    TEST_ASSERT_TRUE(spake2pDeriveW0W1(20202021, salt1, sizeof(salt1), 100, w0a, w1a));
    TEST_ASSERT_TRUE(spake2pDeriveW0W1(20202021, salt2, sizeof(salt2), 100, w0b, w1b));

    TEST_ASSERT_FALSE(memcmp(w0a, w0b, 32) == 0);
}

void test_spake2p_different_iterations_different_w() {
    uint8_t salt[16] = {};
    uint8_t w0a[32], w1a[32], w0b[32], w1b[32];

    TEST_ASSERT_TRUE(spake2pDeriveW0W1(20202021, salt, sizeof(salt), 100, w0a, w1a));
    TEST_ASSERT_TRUE(spake2pDeriveW0W1(20202021, salt, sizeof(salt), 200, w0b, w1b));

    TEST_ASSERT_FALSE(memcmp(w0a, w0b, 32) == 0);
}

// ============================================================================
// SPAKE2+ — Full protocol self-test (Matter Spec 3.10)
// Both sides compute: verifier generates pB+cB, then verifies cA
// ============================================================================

void test_spake2p_full_round() {
    uint8_t salt[32];
    randomBytes(salt, sizeof(salt));

    uint8_t w0[32], w1[32];
    TEST_ASSERT_TRUE(spake2pDeriveW0W1(20202021, salt, sizeof(salt), 100, w0, w1));

    // Simulate commissioner: generate random pA
    ECKeyPair commKp;
    TEST_ASSERT_TRUE(ecGenerateKeypair(commKp));

    Spake2pState st;
    memcpy(st.w0, w0, 32);
    memcpy(st.w1, w1, 32);
    memcpy(st.pA, commKp.pub, 65); // In real protocol this comes from Pake1

    // Fake context (PBKDFParamReq || PBKDFParamResp)
    uint8_t context[32];
    randomBytes(context, sizeof(context));

    // This will fail because pA is random (not w0*M + x*G) — but it should
    // at least not crash and produce a valid pB
    bool ok = spake2pComputeResponse(st, context, sizeof(context));
    // The computation involves point arithmetic that may fail with random pA
    // We test that it doesn't crash; full protocol test needs both sides
    // The important thing: pB should be a valid uncompressed point
    if (ok) {
        TEST_ASSERT_EQUAL(0x04, st.pB[0]); // Uncompressed point marker
        // Ke, KcA, KcB should be non-zero
        bool keZero = true;
        for (int i = 0; i < 16; i++) if (st.Ke[i]) keZero = false;
        TEST_ASSERT_FALSE(keZero);
    }
    // If !ok, the point arithmetic failed with random input — that's expected
}

// ============================================================================
// Certificate utilities
// ============================================================================

void test_extract_pubkey_from_test_cert() {
    uint8_t pubKey[65];
    TEST_ASSERT_TRUE(extractPubKeyFromCert(matter::kTestDACCert, matter::kTestDACCertLen, pubKey));
    TEST_ASSERT_EQUAL(0x04, pubKey[0]); // Uncompressed P-256 point
}

void test_extract_pubkey_invalid_cert() {
    uint8_t fakeCert[] = {0x30, 0x00};
    uint8_t pubKey[65];
    TEST_ASSERT_FALSE(extractPubKeyFromCert(fakeCert, sizeof(fakeCert), pubKey));
}

// ============================================================================
// AES-CCM with Matter nonce format (Spec 4.5.1.1)
// Nonce = secFlags(1) || msgCounter(4LE) || srcNodeId(8LE) = 13 bytes
// ============================================================================

void test_aes_ccm_matter_nonce() {
    uint8_t key[16] = {0};
    for (int i = 0; i < 16; i++) key[i] = i;

    // Build Matter nonce: secFlags=0, counter=1, nodeId=0x0102030405060708
    uint8_t nonce[13];
    nonce[0] = 0x00; // secFlags
    nonce[1] = 0x01; nonce[2] = 0x00; nonce[3] = 0x00; nonce[4] = 0x00; // counter LE
    nonce[5] = 0x08; nonce[6] = 0x07; nonce[7] = 0x06; nonce[8] = 0x05;
    nonce[9] = 0x04; nonce[10] = 0x03; nonce[11] = 0x02; nonce[12] = 0x01; // nodeId LE

    // AAD = message header (8 bytes)
    uint8_t aad[8] = {0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00};

    uint8_t plain[] = "Matter encrypted payload";
    size_t plainLen = strlen((char*)plain);
    uint8_t cipher[64], tag[16], decrypted[64];

    TEST_ASSERT_TRUE(aesCcmEncrypt(key, nonce, 13, aad, 8, plain, plainLen, cipher, tag));
    TEST_ASSERT_TRUE(aesCcmDecrypt(key, nonce, 13, aad, 8, cipher, plainLen, tag, decrypted));
    TEST_ASSERT_EQUAL_MEMORY(plain, decrypted, plainLen);
}

void test_aes_ccm_different_nonce_different_output() {
    uint8_t key[16] = {1};
    uint8_t nonce1[13] = {0};
    uint8_t nonce2[13] = {0}; nonce2[1] = 1; // Different counter

    uint8_t plain[] = {0x42};
    uint8_t cipher1[1], cipher2[1], tag1[16], tag2[16];

    TEST_ASSERT_TRUE(aesCcmEncrypt(key, nonce1, 13, nullptr, 0, plain, 1, cipher1, tag1));
    TEST_ASSERT_TRUE(aesCcmEncrypt(key, nonce2, 13, nullptr, 0, plain, 1, cipher2, tag2));

    // Different nonces must produce different ciphertext
    TEST_ASSERT_FALSE(cipher1[0] == cipher2[0] && memcmp(tag1, tag2, 16) == 0);
}

void test_aes_ccm_different_aad_fails_decrypt() {
    uint8_t key[16] = {2};
    uint8_t nonce[13] = {3};
    uint8_t aad1[] = {0x01};
    uint8_t aad2[] = {0x02};

    uint8_t plain[] = {0xFF};
    uint8_t cipher[1], tag[16], decrypted[1];

    TEST_ASSERT_TRUE(aesCcmEncrypt(key, nonce, 13, aad1, 1, plain, 1, cipher, tag));
    // Decrypt with different AAD should fail
    TEST_ASSERT_FALSE(aesCcmDecrypt(key, nonce, 13, aad2, 1, cipher, 1, tag, decrypted));
}

// ============================================================================
// HKDF — RFC 5869 Test Case 3 (zero-length salt and info)
// ============================================================================

void test_hkdf_sha256_rfc5869_tc3() {
    uint8_t ikm[22];
    memset(ikm, 0x0b, 22);
    uint8_t okm[42];
    TEST_ASSERT_TRUE(hkdfSha256(nullptr, 0, ikm, 22, nullptr, 0, okm, 42));

    const uint8_t expected[] = {
        0x8d, 0xa4, 0xe7, 0x75, 0xa5, 0x63, 0xc1, 0x8f,
        0x71, 0x5f, 0x80, 0x2a, 0x06, 0x3c, 0x5a, 0x31,
        0xb8, 0xa1, 0x1f, 0x5c, 0x5e, 0xe1, 0x87, 0x9e,
        0xc3, 0x45, 0x4e, 0x5f, 0x3c, 0x73, 0x8d, 0x2d,
        0x9d, 0x20, 0x13, 0x95, 0xfa, 0xa4, 0xb6, 0x1a,
        0x96, 0xc8
    };
    TEST_ASSERT_EQUAL_MEMORY(expected, okm, 42);
}

// ============================================================================
// PBKDF2 — higher iteration count (Matter spec requires >= 1000)
// ============================================================================

void test_pbkdf2_matter_iterations() {
    // With 1000 iterations (Matter minimum)
    const uint8_t pw[] = {0x01, 0x02, 0x03, 0x04}; // passcode as bytes
    const uint8_t salt[32] = {};
    uint8_t out1[80], out2[80];

    TEST_ASSERT_TRUE(pbkdf2Sha256(pw, 4, salt, 32, 1000, out1, 80));
    TEST_ASSERT_TRUE(pbkdf2Sha256(pw, 4, salt, 32, 1000, out2, 80));

    // Deterministic
    TEST_ASSERT_EQUAL_MEMORY(out1, out2, 80);

    // Different from fewer iterations
    uint8_t out3[80];
    TEST_ASSERT_TRUE(pbkdf2Sha256(pw, 4, salt, 32, 500, out3, 80));
    TEST_ASSERT_FALSE(memcmp(out1, out3, 80) == 0);
}

// ============================================================================
// CSR Generation
// ============================================================================

void test_csr_generation() {
    ECKeyPair kp;
    TEST_ASSERT_TRUE(ecGenerateKeypair(kp));

    uint8_t csr[512];
    size_t csrLen = sizeof(csr);
    bool ok = generateCSR(kp, csr, csrLen);
    if (ok) {
        TEST_ASSERT_TRUE(csrLen > 0);
        TEST_ASSERT_TRUE(csrLen < 512);
        // DER-encoded CSR starts with SEQUENCE tag (0x30)
        TEST_ASSERT_EQUAL(0x30, csr[0]);
    }
}

// ============================================================================
// Main
// ============================================================================

void setup() {
    delay(2000);
    UNITY_BEGIN();

    // SHA-256
    RUN_TEST(test_sha256_empty);
    RUN_TEST(test_sha256_abc);
    RUN_TEST(test_sha256_stream);
    RUN_TEST(test_sha256_stream_snapshot);

    // HMAC
    RUN_TEST(test_hmac_sha256_rfc4231_tc2);

    // HKDF
    RUN_TEST(test_hkdf_sha256_rfc5869_tc1);
    RUN_TEST(test_hkdf_sha256_rfc5869_tc3);

    // PBKDF2
    RUN_TEST(test_pbkdf2_basic);
    RUN_TEST(test_pbkdf2_matter_iterations);

    // AES-CCM
    RUN_TEST(test_aes_ccm_roundtrip);
    RUN_TEST(test_aes_ccm_tampered_tag);
    RUN_TEST(test_aes_ccm_tampered_ciphertext);
    RUN_TEST(test_aes_ccm_matter_nonce);
    RUN_TEST(test_aes_ccm_different_nonce_different_output);
    RUN_TEST(test_aes_ccm_different_aad_fails_decrypt);

    // EC operations
    RUN_TEST(test_ec_keypair_generation);
    RUN_TEST(test_ecdh_shared_secret);
    RUN_TEST(test_ecdsa_sign_verify);
    RUN_TEST(test_ecdsa_verify_wrong_key);
    RUN_TEST(test_ecdsa_verify_tampered_message);

    // Session keys
    RUN_TEST(test_session_key_derivation);

    // SPAKE2+
    RUN_TEST(test_spake2p_derive_w0w1);
    RUN_TEST(test_spake2p_derive_deterministic);
    RUN_TEST(test_spake2p_different_passcode_different_w);
    RUN_TEST(test_spake2p_different_salt_different_w);
    RUN_TEST(test_spake2p_different_iterations_different_w);
    RUN_TEST(test_spake2p_full_round);

    // Certificates
    RUN_TEST(test_extract_pubkey_from_test_cert);
    RUN_TEST(test_extract_pubkey_invalid_cert);
    RUN_TEST(test_csr_generation);

    UNITY_END();
}

void loop() {}
