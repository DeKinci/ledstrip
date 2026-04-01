# MicroProtoMatter

Matter protocol implementation for ESP32. Bridges MicroProto properties into the Matter ecosystem so the device can be controlled by Apple Home, Google Home, Alexa, and other Matter controllers — without a separate Matter SDK.

## Role

MicroProtoMatter exists so that:

- The device is controllable via Matter without changing its internal architecture — existing MicroProto properties (on/off, brightness, color) map directly to Matter clusters
- Commissioning, encryption, and the full Matter handshake are handled internally — the app just maps properties
- The device appears as a standard Matter light to any commissioner

This is a translational layer: Matter speaks TLV over encrypted UDP, MicroProto speaks binary over WebSocket/BLE. MicroProtoMatter sits between them, translating attribute reads/writes and commands into property get/set calls.

## What It Is Not

- Not a general Matter SDK — it implements the subset needed for a light device (OnOff, LevelControl, ColorControl clusters)
- Not a replacement for MicroProto — Matter is an additional control path alongside the web UI and BLE
- Not multi-device — one session, one fabric, one endpoint (plus endpoint 0 for commissioning)

## Architecture

```
Apple Home / Google Home / Alexa
    ↓ Matter over UDP :5540
MatterTransport — packet handling, mDNS, send/receive
    ↓
MatterSession — PASE (commissioning) or CASE (operational) handshake
    ↓ encrypted channel (AES-128-CCM)
MatterIM — Interaction Model: read, write, invoke, subscribe
    ↓
ClusterBinding — maps Matter attributes/commands ↔ MicroProto properties
    ↓
Property<bool> onOff, Property<uint8_t> brightness, ArrayProperty<uint8_t,3> color
```

## Usage

```cpp
#include <MatterTransport.h>

matter::MatterTransport matter;

void setup() {
    matter.mapOnOff(powerOn);         // Property<bool>
    matter.mapLevel(brightness);       // Property<uint8_t>
    matter.mapColor(color);            // ArrayProperty<uint8_t, 3>
    matter.begin();                    // starts UDP listener + mDNS
}

void loop() {
    matter.loop();                     // handles packets, MRP retries, subscriptions
}
```

## Commissioning Flow (PASE)

First-time setup. Commissioner (phone) and device establish a shared secret from a known passcode via SPAKE2+:

```
Commissioner                          Device
    │                                    │  (state: PASE_WAIT_PBKDF_REQ)
    ├── PBKDFParamRequest ──────────────►│
    │◄── PBKDFParamResponse (salt, iter) │  (state: PASE_WAIT_PAKE1)
    ├── Pake1 (pA) ────────────────────►│
    │◄── Pake2 (pB, cB) ────────────────│  (state: PASE_WAIT_PAKE3)
    ├── Pake3 (cA) ────────────────────►│
    │◄── StatusReport (success) ─────────│  (state: PASE_ACTIVE)
    │                                    │
    ├── CSRRequest ─────────────────────►│  → generates keypair, returns CSR
    ├── AddNOC (cert chain) ────────────►│  → stores NOC + ICAC + root cert
    ├── CommissioningComplete ──────────►│  → saves fabric to flash
    │                                    │  (state: CASE_WAIT_SIGMA1)
```

After commissioning, the device persists fabric credentials (NOC, operational key, IPK) to flash and switches to CASE for all future connections.

## Operational Flow (CASE)

Subsequent connections use certificate-based authentication with ECDHE key exchange:

```
Commissioner                          Device
    │                                    │  (state: CASE_WAIT_SIGMA1)
    ├── Sigma1 (ephemeral pubkey) ──────►│
    │◄── Sigma2 (encrypted NOC + sig) ───│  (state: CASE_WAIT_SIGMA3)
    ├── Sigma3 (encrypted NOC + sig) ───►│
    │◄── StatusReport (success) ─────────│  (state: CASE_ACTIVE)
    │                                    │
    ├── ReadRequest (brightness) ───────►│  → reads from MicroProto property
    │◄── ReportData (value: 200) ────────│
    ├── InvokeRequest (Toggle) ─────────►│  → onOff.set(!onOff.get())
```

## Cluster Mapping

| Matter Cluster | Attribute/Command | MicroProto Property |
|---------------|-------------------|---------------------|
| OnOff (0x0006) | OnOff attribute | `Property<bool>` |
| OnOff | Off/On/Toggle commands | `.set(false)` / `.set(true)` / `.set(!get())` |
| LevelControl (0x0008) | CurrentLevel | `Property<uint8_t>` |
| LevelControl | MoveToLevel command | `.set(level)` |
| ColorControl (0x0300) | CurrentHue, CurrentSaturation | `ArrayProperty<uint8_t, 3>` (RGB, converted) |
| ColorControl | MoveToHueAndSaturation | RGB↔HSV conversion + `.set()` |

## Crypto

All implemented via mbedTLS (on device) with test stubs for native builds:

- **SPAKE2+** — password-authenticated key exchange (RFC 9383)
- **ECDHE P-256** — ephemeral key agreement for CASE
- **ECDSA P-256** — certificate signing/verification
- **AES-128-CCM** — authenticated encryption for all operational messages
- **HKDF-SHA256** — session key derivation
- **PBKDF2-SHA256** — passcode stretching

## Message Reliability (MRP)

UDP is unreliable, so MRP adds:

- 32-entry sliding window for duplicate detection
- Piggybacked ACKs on response messages
- Exponential backoff retransmission (300ms base, 5 retries, 10s cap)

## mDNS

- **Commissioning**: `_matterc._udp` with discriminator, device type, pairing hint
- **Operational**: `_matter._udp` with compressed fabric ID + node ID

## Persistence

Stored in flash (NVS/Preferences):

- Operational keypair (generated during CSR)
- NOC, ICAC, root certificate
- Fabric ID, node ID, IPK
- Boot count (via microlog-up)

## Memory

Single ~9KB allocation on `begin()`. Fixed 1280-byte RX/TX buffers (Matter max message size).

## Dependencies

- microproto (property types for cluster binding)
- microlog (structured logging)
- mbedTLS (ESP-IDF, for all crypto)
