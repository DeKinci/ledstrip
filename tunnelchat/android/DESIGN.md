# tunnelchat/android — design

Public API surface and internal layering for the Android Kotlin library. This is a design doc for validation; no code is scaffolded yet.

## Target stack

- **Language**: Kotlin
- **Async**: `suspend` + `Flow`
- **Min SDK**: 26 (Android 8.0)
- **Permissions**: `BLUETOOTH_SCAN`, `BLUETOOTH_CONNECT` (API 31+). Library returns typed errors on missing permissions; the app requests them.
- **Persistence**: SQLite via Room (internal only — no Room types in the public API).
- **Protobuf**: `com.google.protobuf:protobuf-javalite` (smaller APK, coroutine-friendly).
- **Package root**: `io.tunnelchat`.

## Layering

```
┌─────────────────────────────────────────────────────────┐
│  Public API                    Tunnelchat (entry point) │
│  (api/*)                       Message, Envelope,       │
│                                Presence, PeerInfo,      │
│                                BlobHandle, ProtoArrival,│
│                                ProtoRegistry,           │
│                                Statistics, DiagLog      │
├─────────────────────────────────────────────────────────┤
│  Proto layer                   ProtoSender / Receiver   │
│  (internal/proto)              ProtoEnvelope codec      │
│                                Built-in Echo/Pong       │
├─────────────────────────────────────────────────────────┤
│  Orchestration                 CommandDispatcher        │
│  (internal/protocol)           GapFiller                │
│                                SessionCoordinator       │
├─────────────────────────────────────────────────────────┤
│  Blob layer                    BlobSender / Receiver    │
│  (internal/blob)               BlobEnvelope codec       │
├─────────────────────────────────────────────────────────┤
│  Feature modules               StatsRecorder            │
│  (internal/stats, log)         DiagLog                  │
├─────────────────────────────────────────────────────────┤
│  Archive                       MessageStore             │
│  (internal/archive)            PresenceStore            │
│                                StatsStore, LogStore     │
├─────────────────────────────────────────────────────────┤
│  Wire codec                    CommandFrame codec       │
│  (internal/wire)               NotificationFrame codec  │
├─────────────────────────────────────────────────────────┤
│  BLE transport                 BleSession               │
│  (internal/ble)                ConnectionStateMachine   │
│                                GattCallback, MtuHandler │
└─────────────────────────────────────────────────────────┘
```

Each layer only talks to the one directly below. Proto sits above Blob (protos are serialized, then handed to the blob transport for chunking + delivery).

## Public API sketch

```kotlin
class Tunnelchat(context: Context, config: TunnelchatConfig) : Closeable {

    // Connection lifecycle
    val connectionState: StateFlow<ConnectionState>
    suspend fun pair(device: BluetoothDevice): Result<Unit>
    suspend fun connect(): Result<Unit>
    suspend fun disconnect()

    // Messaging (typed I/O — opaque payloads)
    suspend fun sendText(bytes: ByteArray): Result<MessageEnvelope>
    suspend fun sendLocation(nodeA: UByte, nodeB: UByte): Result<MessageEnvelope>

    // Reactive streams
    val incomingMessages: SharedFlow<MessageEnvelope>
    val peers: StateFlow<Map<SenderId, PeerInfo>>
    fun messageHistory(sender: SenderId, fromSeq: UShort? = null): Flow<MessageEnvelope>

    // Blob transport (raw bytes)
    suspend fun sendBlob(bytes: ByteArray, tag: String? = null): BlobHandle
    val incomingBlobs: SharedFlow<BlobArrival>                             // completed, receiver-CRC-verified
    val incomingBlobsInFlight: StateFlow<Map<BlobId, BlobReceiveProgress>> // live partial state

    // Protobuf transport (typed messages on top of Blob)
    val protoRegistry: ProtoRegistry
    suspend fun <T : MessageLite> sendProto(schemaId: UShort, message: T): BlobHandle
    val incomingProtos: SharedFlow<ProtoArrival>

    // Self-info
    suspend fun selfInfo(): Result<SelfInfo>
    suspend fun setClock(unixSeconds: UInt): Result<Unit>

    // Diagnostics
    val statistics: StateFlow<Statistics>
    fun resetStatistics()
    val diagnosticLog: DiagnosticLog
    suspend fun echoProbe(target: SenderId): Result<EchoResult>   // no-op if config.debugMode == false

    override fun close()
}
```

### Config

```kotlin
data class TunnelchatConfig(
    val archivePath: File,
    val logBufferBytes: Long = 2 * 1024 * 1024,
    val debugMode: Boolean = false,
    val echoIntervalMs: Long = 60_000,
    val maxBlobBytes: Int = 2048,                     // ~26 chunks, ~8s mesh airtime
    val maxBlobBytesHardCeiling: Int = 4096,          // caller can raise maxBlobBytes up to this
    val maxInFlightBlobBytes: Int = 8 * 1024,         // total-bytes cap across parallel sends; excess → Queued
    val partialBlobGcMs: Long = 10 * 60_000,          // receiver drops partial reassembly after this idle
)
```

Chunk payload size is not configurable — it's derived from the firmware's 99-byte Text cap minus the 20-byte blob envelope = **79 bytes/chunk**.

### Core types

```kotlin
@JvmInline value class SenderId(val raw: UByte)
@JvmInline value class Seq(val raw: UShort)
@JvmInline value class BlobId(val raw: ULong)   // 8 raw bytes on the wire; no UUID wrapping

sealed class Message {
    data class Text(val bytes: ByteArray) : Message()
    data class Location(val nodeA: UByte, val nodeB: UByte) : Message()
    data class Opaque(val msgType: UByte, val payload: ByteArray) : Message()
}

data class MessageEnvelope(
    val senderId: SenderId,
    val seq: Seq,
    val timestamp: UInt,
    val receivedAtMs: Long,
    val message: Message,
    val delivery: DeliveryState,
)

/**
 * Sender-side delivery state for outbound messages.
 *
 * v1 transitions: `Pending → SentToAbonent → Failed`. `MeshVisible` is a reserved slot —
 * no firmware signal exists today to drive it (`bleSendText` / `bleSetLocation` do not
 * ack the BLE command and do not loop own outbound back via `IncomingMsg`). A firmware
 * proposal is filed in `device/retranslator/FIRMWARE_TODO.md`; when it lands the library
 * will begin emitting `MeshVisible` without API churn.
 *
 * `BlobAcked` is deliberately absent — the mesh is broadcast + digest-sync pull, and the
 * sender cannot know whether any given peer reassembled a blob. Receiver-side completion
 * lives on `incomingBlobs`.
 */
enum class DeliveryState { Pending, SentToAbonent, MeshVisible, Failed }

sealed class ConnectionState {
    object Disconnected : ConnectionState()
    object Scanning : ConnectionState()
    data class Connecting(val device: BluetoothDevice) : ConnectionState()
    data class Connected(val device: BluetoothDevice, val mtu: Int) : ConnectionState()
    data class Reconnecting(val attempt: Int) : ConnectionState()
    data class Error(val err: TunnelchatError) : ConnectionState()
}

enum class Presence { Online, Stale, Offline }

data class PeerInfo(
    val senderId: SenderId,
    val presence: Presence,
    val lastHeardMs: Long,
    val highSeq: Seq,
    val lastLocation: Message.Location?,
)

data class SelfInfo(
    val deviceId: UByte,
    val clockUnix: UInt,
    val activeSenders: UByte,
    val bootCount: UInt,
)

sealed class TunnelchatError {
    object PermissionMissing : TunnelchatError()
    object BluetoothDisabled : TunnelchatError()
    object NotPaired : TunnelchatError()
    object Timeout : TunnelchatError()
    data class BleGatt(val statusCode: Int) : TunnelchatError()
    data class PayloadTooLarge(val limitBytes: Int) : TunnelchatError()
    data class UnknownSchema(val schemaId: UShort) : TunnelchatError()
    data class Internal(val cause: Throwable) : TunnelchatError()
}
```

### Blob API

```kotlin
class BlobHandle internal constructor(...) {
    val blobId: BlobId
    val progress: StateFlow<BlobSendProgress>
    /** Suspends until every chunk has been handed to the BLE transport (or the send fails).
     *  Does NOT imply any peer reassembled the blob — the mesh gives no such signal. */
    suspend fun awaitTransmitted(): Result<Unit>
    fun cancel()
}

sealed class BlobSendProgress {
    /** Byte-budget cap hit; waiting for in-flight sends to free capacity. */
    data class Queued(val totalChunks: Int) : BlobSendProgress()
    /** Actively transmitting chunks to the BLE transport. */
    data class InFlight(val totalChunks: Int, val sentChunks: Int) : BlobSendProgress()
    /** All chunks handed to transport. Delivery to mesh peers is not observable by sender. */
    data class Transmitted(val totalChunks: Int) : BlobSendProgress()
    data class Failed(val error: TunnelchatError) : BlobSendProgress()
}

data class BlobReceiveProgress(
    val blobId: BlobId,
    val senderId: SenderId,
    val receivedChunks: Int,
    val totalChunks: Int,
    val firstSeenAtMs: Long,
)

/** Completed, CRC-verified reassembly. `hash` is receiver-local truth (e.g., SHA-256). */
data class BlobArrival(val blobId: BlobId, val senderId: SenderId, val bytes: ByteArray, val hash: ByteArray, val tag: String?)

data class EchoResult(val rttMs: Long, val pingId: UInt)
```

### Proto API

```kotlin
class ProtoRegistry {
    /** Register a parser so incoming protos with [schemaId] can be decoded.
     *  Library-reserved IDs are 0..255; app IDs start at 256. */
    fun <T : MessageLite> register(schemaId: UShort, parser: Parser<T>)
    fun unregister(schemaId: UShort)
}

sealed class ProtoArrival {
    abstract val senderId: SenderId
    abstract val schemaId: UShort
    data class Known(
        override val senderId: SenderId,
        override val schemaId: UShort,
        val message: MessageLite,
    ) : ProtoArrival()
    data class Unknown(
        override val senderId: SenderId,
        override val schemaId: UShort,
        val rawBytes: ByteArray,
    ) : ProtoArrival()
}
```

### Reserved library schema IDs (0..255)

| ID | Type | Purpose |
|---:|---|---|
| 1 | `Echo` | Ping probe; originator sends, peers auto-reply with `Pong`. |
| 2 | `Pong` | Reply to `Echo`, carries origin timestamp for RTT computation. |
| 3..255 | *(reserved)* | For future built-in types — protocol metadata, acks, etc. |

`Echo`/`Pong` replace the earlier ad-hoc marker-based echo scheme. The debug-gated `echoProbe()` internally calls `sendProto(1, Echo(...))` and awaits a matching `Pong`.

### Wire envelopes (inside `Text` payload)

**Blob chunk envelope (20 bytes):**
```
[marker:4][blob_id:8][idx:2][total:2][crc32:4][chunk_bytes:N]
```

**Proto envelope (serialized proto bytes, sent *via* blob transport):**
```
[proto_magic:1=0xA7][schema_id:2][proto_bytes:N]
```

Receive-side disambiguation for a `Text` message:

1. Starts with blob marker? Try parse as blob envelope. Sanity-check `idx < total`, `total < 256`, CRC32 over `chunk_bytes` matches. All pass → blob chunk.
2. Not a blob → emit as plain text to `incomingMessages`.
3. When a blob completes, check its payload for proto magic byte `0xA7`. If yes → emit on `incomingProtos`. Otherwise → `incomingBlobs`.

Marker collision risk: marker alone is ~2^-32; combined with the CRC gate, collision is ~2^-64. Negligible.

## Internal module sketch

| Module | Responsibility |
|---|---|
| `internal/ble/BleSession` | Owns `BluetoothGatt`, service discovery, MTU. |
| `internal/ble/ConnectionStateMachine` | Tracks states, reconnect backoff. |
| `internal/ble/GattCallbackBridge` | Bridges Android GATT callbacks to coroutines/flows. |
| `internal/wire/CommandFrame` | Encodes BLE commands 0x01..0x06. |
| `internal/wire/NotificationFrame` | Decodes BLE responses/notifications 0x80..0x84. |
| `internal/protocol/CommandDispatcher` | Serializes outbound commands, correlates request→response. |
| `internal/protocol/GapFiller` | On reconnect: `GetState` → diff vs local → `GetMessages`. |
| `internal/protocol/SessionCoordinator` | Glues dispatcher, archive, incoming streams. |
| `internal/archive/MessageStore` | Per-sender seq-indexed messages. |
| `internal/archive/PresenceStore` | Last-heard timestamps + presence derivation. |
| `internal/archive/StatsStore` | Periodic counter flush. |
| `internal/archive/LogStore` | Rolling log file(s). |
| `internal/blob/BlobSender` | Chunker, per-chunk transmit, byte-budget gate, progress `StateFlow`. No end-to-end ACK. |
| `internal/blob/BlobReceiver` | Reassembler, CRC verifier, partial-state flow, 10-min idle GC. |
| `internal/blob/BlobEnvelope` | 20-byte envelope codec. |
| `internal/proto/ProtoSender` | Wraps `MessageLite` in envelope, hands to `BlobSender`. |
| `internal/proto/ProtoReceiver` | Inspects completed blobs for proto magic, dispatches to registry. |
| `internal/proto/BuiltinEcho` | `Echo`/`Pong` handling; auto-reply to incoming `Echo` (schema 1). |
| `internal/stats/StatsRecorder` | Counter/gauge/histogram primitives. |
| `internal/log/DiagLogImpl` | Level-filtered rolling buffer writer. |

## Key design decisions

1. **One `Tunnelchat` per process.** Holds BLE session + DB + flows. Enforced informally; documented in KDoc.
2. **Immutable snapshots** for `Statistics`, `PeerInfo`, `ConnectionState`.
3. **`Result<T>` on fallible suspending calls.** Exceptions only for programmer bugs.
4. **Blob transport is real, not a stub.** Ships day-one using Text tunneling (firmware-validated byte-clean). If firmware later gains a dedicated `Bytes` msg_type, the internal transport swaps; the public API is unchanged.
5. **Blob send progress via `BlobHandle`**; blob receive progress via `incomingBlobsInFlight`. Apps can render "12/29 chunks received from Bob" and transition to the completed view when `incomingBlobs` fires. The sender sees `Queued → InFlight → Transmitted` (or `Failed`); the mesh is broadcast + digest-sync pull, so there is no end-to-end delivery signal.
6. **Gap-fill is automatic for text** (atomic messages, just appear when ready). Blobs expose partial-state because chunked delivery is user-visible by nature.
7. **Marker + envelope + CRC for blob disambiguation.** A text payload is treated as a blob chunk only if it starts with the 4-byte marker AND parses as a sane envelope AND the CRC32 over chunk bytes matches. Collision probability is negligible (~2^-64). A dedicated firmware `Bytes` msg_type would supplant this — tracked as a proposal in `FIRMWARE_TODO.md`.
8. **Proto sits above blob, not beside it.** Protos are serialized then sent through the blob transport. Keeps blob API independent; proto layer is optional and adds no overhead if unused.
9. **Registry-based proto schema IDs.** 5-byte wire overhead (1 magic + 2 blob-layer proto tag + 2 schema_id — the proto magic + schema_id lives inside the reassembled blob payload, not per chunk). Reserved range 0..255 for library built-ins; app uses 256..65535.
10. **Built-in Echo/Pong as proto types.** The library registers schemas 1 and 2 itself and handles auto-reply internally — apps never see Echo/Pong in `incomingProtos`.
11. **`debugMode` gates**: verbose payload logging, echo probe. One flag.
12. **No Room types leak out.** DAO stays internal.
13. **Pairing is manual.** App passes in a `BluetoothDevice` (scanning/UI is the app's job). Method is named generically (`pair`, not `pairAbonent`) so a future phone-to-phone peer type fits without API churn.
14. **`messageHistory(sender, fromSeq)` is a cold snapshot `Flow`.** It emits the backlog once and completes. Live updates come from composing with `incomingMessages.filter { it.senderId == sender }`. A UI "refresh" is just a re-invocation. Rationale: a single hot flow confuses "have I already seen this?" — backlog-vs-live is a user-visible distinction and deserves distinct streams.
15. **Archive is shared across paired abonents (v1).** One Room DB per `Tunnelchat` instance. Swapping abonents does not wipe history; the user's own outbound thread splits by `sender_id` if their own device ID differs. Acceptable for v1; revisit if UX complaints surface.
16. **Presence is device-authoritative.** The phone mirrors `PresenceEvent` pushes (byte 0=online / 1=stale / 2=offline). On reconnect, the phone re-derives current presence from the per-entry presence byte in the `StateResp` response.
17. **Parallel blob sends are gated by a total-bytes cap** (`maxInFlightBlobBytes`, default 8 KB). Sends that would exceed the cap sit in `BlobSendProgress.Queued` until capacity frees; this prevents runaway mesh airtime usage without serialising the public API.

## Notes & future work

Captured here so future contributors don't re-litigate decisions or miss known expansion points.

### Echo/Pong clock handling

The echo probe measures round-trip time against the **originator's own clock** — the responding peer echoes back the origin timestamp verbatim, and the originator computes `rttMs = now - origin`. No assumption of synchronized clocks between peers. Do not try to compute one-way latency by subtracting timestamps across phones; clock skew makes it meaningless.

### Why registry over hashed-FQDN for proto schema IDs

We considered auto-deriving schema IDs from a hash of the proto's fully qualified name (option B in earlier design discussion). Rejected because:
- **Rename-unsafe**: renaming a Kotlin/proto class changes the hash, silently breaking all existing peers.
- **Inconsistent with protobuf's own model**: protobuf field numbers are explicit and stable; schema IDs following the same philosophy is less surprising.
- **Operational burden of a registry is negligible** at the small-team scale this library targets — a table with ~50 entries over the library's lifetime.

Reserved range 0..255 is for library built-ins (`Echo`/`Pong` today, future protocol metadata). Apps use 256..65535.

### Known forward-compatibility workarounds

The library currently relies on two things that the firmware author may or may not accept as proposals:

1. **Marker + envelope + CRC32 blob disambiguation** is a workaround for the absence of a dedicated `Bytes` msg_type. Robust (~2^-64 false-positive probability) but adds 20 bytes of envelope to every chunk. If the firmware ever adds a `Bytes` type, the internal blob transport should switch to it and the marker scheme can be retired. Public API is unchanged either way.
2. **Mesh extensibility** depends on firmware accepting `validatePayload()` `default: return true` and a `default:` branch in `Relay::handleLoRaMessage`. Without these, no new `msg_type` can be added without re-flashing every node in the mesh. See `device/retranslator/FIRMWARE_TODO.md`.

These are proposals to the protocol author, not commitments. The library ships fully functional today regardless; these notes flag where future firmware decisions would let us simplify.

### Future scope (explicitly not v1)

- **Phone-to-phone BLE peering**: the `pair(device)` / `connect()` API is generically named to accommodate this without a rename. Would also need a peer-type discriminator and likely new protocol message types at the mesh level.
- **Dedicated `Bytes` msg_type** in firmware: would let blob transport drop the envelope marker + CRC and use msg_type-based dispatch instead.
- **End-to-end encryption** between specific peers (current protocol is broadcast-visible to the mesh).
- **Blob resumption across disconnects**: today a blob transfer interrupted mid-way requires restart. A resumable chunk-index protocol is possible but not v1.

## Planned scaffolding tree

```
tunnelchat/android/
├── README.md               (android-specific: build, quick start, permissions)
├── build.gradle.kts
├── settings.gradle.kts
├── gradle.properties
├── lib/
│   ├── build.gradle.kts
│   ├── src/main/kotlin/io/tunnelchat/
│   │   ├── Tunnelchat.kt
│   │   ├── TunnelchatConfig.kt
│   │   ├── api/ ...                    (public types from this doc)
│   │   └── internal/
│   │       ├── ble/
│   │       ├── wire/
│   │       ├── protocol/
│   │       ├── blob/
│   │       ├── proto/
│   │       ├── archive/
│   │       ├── stats/
│   │       └── log/
│   └── src/main/proto/
│       └── io/tunnelchat/builtin.proto   (Echo, Pong)
└── demo/
    ├── build.gradle.kts
    └── src/main/kotlin/...              (minimal manual-test app)
```
