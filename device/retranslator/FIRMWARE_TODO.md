# Firmware TODO — forward-compat for phone-side blob transport

Before the mobile library can safely tunnel arbitrary bytes through the mesh (and before a future `0x03 Bytes` message type can be added without re-flashing every node), land these three small changes.

## 1. Accept unknown msg_types at decode

`src/message.h` — `Message::validatePayload()`:

```cpp
default:
    return false;  // ← current: drops the packet entirely
```

Change to:

```cpp
default:
    return true;   // accept unknown types as opaque payload
```

Rationale: today, any LoRa packet with a `msg_type` the firmware doesn't recognize fails `decode()` and is silently discarded. That makes the protocol non-extensible — any new type requires a forklift upgrade of every retranslator and abonent in the field.

## 2. Store + relay unknown types

`src/relay.cpp` — `Relay::handleLoRaMessage` switch has no `default` branch. Add:

```cpp
default:
    handleLiveMessage(msg);  // store, dedup, relay, push to BLE app
    break;
```

So unknown types flow through the normal store-and-forward path.

## 3. Fix `MAX_MSG_PAYLOAD` / PROTOCOL.md mismatch

- `src/message.h:8` — `#define MAX_MSG_PAYLOAD 100`
- `PROTOCOL.md` — claims "up to ~190 bytes"

Pick one and make both agree. Effective Text data capacity today is **99 bytes** (1 length byte + 99 data), not ~190.

## 4. Signal "mesh-visible" for own outbound

`src/relay.cpp` — `bleSendText()` / `bleSetLocation()` currently do two things the phone can't observe:

1. They do **not** ack the BLE command (no response notification on successful write).
2. They do **not** loop the own outbound back via `IncomingMsg` (only remote-origin LoRa messages hit `blePushMessage`).

Result: the phone can confirm "I handed the bytes to BLE" but cannot confirm the device actually broadcast them. The mobile library's `DeliveryState.MeshVisible` has no signal to drive and stays unused.

Pick one:

**Option A — command ack** (cheaper). Add a response byte, e.g. `BLE_RESP_SENT=0x85`, payload `[msg_type:1][seq:2]`, sent from `bleSendText` / `bleSetLocation` after the local store + LoRa write succeed.

**Option B — loop own-outbound via `IncomingMsg`** (richer, reuses existing path). Call `blePushMessage(ownEntry)` from `bleSendText` / `bleSetLocation` after storing. Phone then sees its own seq via the same `0x82` push it already handles.

Either choice lets the library flip `Pending → SentToAbonent → MeshVisible` without public API changes.

## 5. PROTOCOL.md doc patches (no code change)

Already applied alongside this TODO:
- Document `BLE_RESP_SELF_INFO=0x84` (11-byte frame) in the Device→App table.
- Clarify `StateResp` entry size: 8 bytes per entry `[senderId, highSeq:2, locSeq:2, nodeA, nodeB, presence]`, not 5. The 5-byte shape is the LoRa-side `Digest` payload, a different message.

## Why these matter for the mobile library

The Android library's initial blob-transport implementation will tunnel arbitrary bytes inside `Text` messages (byte-clean end-to-end — validated in src, zero encoding overhead). That works on current firmware.

But once you decide to add a dedicated `0x03 Bytes` type — for clean semantics, different storage policy, or cross-client clarity — every node in the mesh must already tolerate unknown types, or the upgrade is all-or-nothing. Landing (1) and (2) now costs ~4 lines and buys permanent forward compatibility.
