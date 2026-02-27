# Retranslator Sync Protocol

## Overview

Underground tunnel mesh protocol for delay-tolerant networking. Nodes (roaming abonents with BLE apps + stationary retranslators) maintain a shared message log and sync it via LoRa when in range.

### Node Types

- **Abonent**: roaming, has BLE app, produces messages and location updates, clock synced via app
- **Retranslator**: stationary, no app, relay+storage only, no own messages

All nodes are peers for sync. Same protocol, same storage, same relay logic.

### Two Delivery Paths

1. **Live broadcast** — new message created via BLE app → immediately sent on LoRa. Any node in range gets it instantly, stores it, pushes to BLE app. Zero latency for neighbors.
2. **Digest sync** — catches up everything missed while out of range. Beacon detects neighbor, digest exchange identifies gaps, missing messages sent newest-first (most important first).

Both paths coexist. Live broadcast handles real-time chat when people are near each other. Digest sync handles store-and-forward when people roam between disconnected nodes.

### Chain Propagation (A→B→C→D)

1. A creates message (seq=5), broadcasts on LoRa. B is in range, stores it.
2. B roams to C's range. Beacons detect hash mismatch. B sends digest, C requests missing messages. C now has A's message.
3. C's next beacon exchange with D propagates A's data further.

Each hop: ~10-15 seconds (beacon interval + digest exchange). State travels across the full chain as people move.

## Wire Format

### Common Header (8 bytes)

```
[sender_id:1][seq:2][timestamp:4][msg_type:1][payload:N]
```

- **sender_id** (1 byte): originating abonent ID (0x01-0xFE). Retranslators don't originate, they relay.
- **seq** (2 bytes): per-sender monotonic sequence number, big-endian. Used for sync and dedup.
- **timestamp** (4 bytes): unix seconds, big-endian. Set from BLE app clock.
- **msg_type** (1 byte): message type (see below).
- **payload** (N bytes): type-dependent, up to ~190 bytes.

Deduplication is by `(sender_id, seq)` pair. No separate msg_id needed.

### Message Types

| Type | Value | Stored | Relayed | Description |
|------|-------|--------|---------|-------------|
| Location | 0x01 | Yes | Live+Sync | Location update |
| Text | 0x02 | Yes | Live+Sync | Text message |
| Beacon | 0x10 | No | No | Periodic neighbor detection |
| Digest | 0x11 | No | No | Sync handshake |
| SyncRequest | 0x12 | No | No | Request missing messages |

### Location Payload (2 bytes)

```
[node_a:1][node_b:1]
```
- `node_b = 0xFF` means "at node_a"
- Otherwise "between node_a and node_b"

### Text Payload (up to ~190 bytes)

```
[len:1][data:N]
```
- UTF-8 text, emoji, etc.

### Beacon Payload (3 bytes)

```
[state_hash:2][node_type:1]
```
- `state_hash`: XOR of `(sender_id ^ seqHi ^ seqLo)` across all stored entries. Changes when any new data is merged.
- `node_type`: 0 = retranslator, 1 = abonent

Sent every 10 seconds. Not stored, not relayed.

### Digest Payload

```
[entry_count:1]
per entry (5 bytes):
  [sender_id:1][high_seq:2][loc_seq:2]
```
- `high_seq`: highest message seq stored for this sender
- `loc_seq`: seq of latest location update for this sender

With 4 abonents: 1 + 4×5 = 21 bytes.

### SyncRequest Payload

```
[entry_count:1]
per entry (5 bytes):
  [sender_id:1][from_seq:2][to_seq:2]
```
"Send me sender X's messages from seq `from_seq` to seq `to_seq`."

## Sync Protocol

### Handshake

```
Node A                          Node B
  |--- Beacon (hash=0xAB) ------->|
  |<-- Beacon (hash=0xCD) --------|  (hashes differ)
  |--- Digest ------------------->|
  |<-- Digest --------------------|
  |  (both compute what they need)
  |<-- SyncRequest ----------------|
  |--- Message (newest first) ---->|
  |--- Message (...) ------------->|
  |<-- SyncRequest (if A needs) ---|
  |<-- Message (...) --------------|
```

Both sides can send SyncRequests simultaneously. Messages are sent one per LoRa packet, **newest first**, round-robin across senders so all senders get fair coverage even if contact breaks early.

### Live Broadcast

```
App sends text via BLE → device stores it → immediately broadcast on LoRa
Any node in range → dedup by (sender_id, seq) → store → push to BLE app if connected
```

Live messages are NOT re-broadcast (no flooding). Propagation beyond direct range happens via digest sync.

### Deduplication

By `(sender_id, seq)` pair. If a message with the same sender_id and seq already exists in the SenderLog, it's dropped. No separate dedup buffer needed.

## Presence

Computed from `millis() - lastHeardMs` per sender:

| State | Condition | Meaning |
|-------|-----------|---------|
| Online | < 2 minutes | Recently active |
| Stale | < 10 minutes | No recent activity |
| Offline | > 10 minutes | Presumed out of range |

Presence is computed, not stored. Offline entries are excluded from state hash and sync payloads to prevent zombie propagation. Entries offline for >1 hour are purged.

## BLE Command Protocol

App communicates via BLE NUS (Nordic UART Service). First byte is command type.

### App → Device (Write to RX characteristic)

| Cmd | Value | Payload | Effect |
|-----|-------|---------|--------|
| SetClock | 0x01 | `[unix_time:4]` | Set device clock |
| SetLocation | 0x02 | `[nodeA:1][nodeB:1]` | Update own location, live broadcast on LoRa |
| SendText | 0x03 | `[len:1][text:N]` | Send text message, live broadcast on LoRa |
| GetState | 0x04 | (empty) | Request all sender digests + locations |
| GetMessages | 0x05 | `[senderId:1][fromSeq:2]` | Stream messages to app |
| GetSelfInfo | 0x06 | (empty) | Request own deviceId, clock, active count |

### Device → App (Notify on TX characteristic)

| Resp | Value | Payload | Trigger |
|------|-------|---------|---------|
| StateResp | 0x80 | `[count:1][entries:N]` | Response to GetState |
| MessageResp | 0x81 | `[senderId:1][seq:2][ts:4][type:1][payload:N]` | Response to GetMessages |
| IncomingMsg | 0x82 | same as MessageResp | Push: new message arrived via LoRa |
| PresenceEvent | 0x83 | `[senderId:1][presence:1]` | Push: presence state changed |

## Data Storage

### Per-sender message logs

Each known sender gets a `SenderLog` — a ring buffer of messages plus metadata:

```
SenderLog:
  senderId, highSeq, locSeq
  nodeA, nodeB (cached latest location)
  lastHeardMs (for presence)
  messages[MSGS_PER_SENDER]  (ring buffer, default 64)
```

### Memory budget (7 nodes, 4 abonents)

```
4 senders × (16 bytes metadata + 64 msgs × ~112 bytes) = ~28 KB
ESP32-S3 has 320KB SRAM → ~9% usage. Comfortable.
```

### LoRa budget per sync

```
Worst case: 256 missing messages × ~60 bytes = ~15 KB → ~50s at 300 bytes/sec air rate
Typical: 5-10 missing messages × ~60 bytes = ~600 bytes → ~2 seconds
Beacons: 11 bytes every 10s per node = negligible
```

## Configuration Defaults

| Parameter | Default | Description |
|-----------|---------|-------------|
| BEACON_INTERVAL_MS | 10000 | Beacon period |
| MAX_SENDERS | 8 | Max tracked senders |
| MSGS_PER_SENDER | 64 | Messages per sender ring buffer |
| MAX_MSG_PAYLOAD | 100 | Max payload bytes per message |
| PRESENCE_STALE_MS | 120000 | 2 min → stale |
| PRESENCE_TIMEOUT_MS | 600000 | 10 min → offline |
| PRESENCE_EXPIRE_MS | 3600000 | 1 hour → purge |

All overridable via `#define` before include or `-D` build flags.
