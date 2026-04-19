# tunnelchat

A mobile client library for talking to Catacombs LoRa mesh devices over Bluetooth.

## What it is

A low-level library that other apps embed to communicate with a paired LoRa "abonent" device, using the mesh protocol defined in [`device/retranslator/PROTOCOL.md`](../device/retranslator/PROTOCOL.md). It handles BLE lifecycle, command framing, local archiving, and blob chunking, and exposes a small async API of primitives (send message, send bytes, observe peers, observe deliveries). It is **not** a chat application — it ships no UI and makes no product decisions. Apps built on top decide what the primitives mean: chat, telemetry, file sharing, coordination, whatever.

**Android first, iOS planned.** Structure:

```
tunnelchat/
├── README.md          (this file — shared design and scope)
├── android/           (Kotlin library — active)
└── ios/               (Swift library — planned, mirrors android API)
```

## The world this library lives in

- **Setting**: underground tunnels, no cell signal.
- **Hardware per user**: a phone + a small LoRa device ("abonent") paired over BLE.
- **Infrastructure**: stationary "retranslator" nodes scattered through the tunnels, relaying and storing messages.
- **Goal**: apps running on people's phones can exchange messages, locations, and small data blobs; the mesh moves bytes across nodes when people are out of direct range. What those messages *mean* to end users is the embedding app's problem.

The phone is the UI and the long-term archive. The abonent is a volatile radio + ~28 KB ring buffer. The mesh is the transport.

## What the library does

The primitives it exposes to embedding apps:

1. **Pair and stay connected** to a LoRa abonent over BLE — discovery, bonding, reconnect, MTU.
2. **Send a text message** — library assigns seq numbers and persists a local copy.
3. **Receive messages** — live pushes from BLE, plus backfill after reconnect, as a reactive stream.
4. **Presence** — observable online / stale / offline state per known sender, derived from `lastHeardMs` with the thresholds fixed by the protocol. This is pure timing and carries no semantics.
5. **Typed message I/O** — send and receive the protocol's message types (`Text`, `Location`, future `Bytes`) as opaque payloads. The library does not interpret them. `Location` carries two node-ID bytes (`nodeA`, `nodeB`); what those IDs mean geographically — which retranslator is at which junction, how to render "between 3 and 4" on a map — is decided entirely by the embedding app. There is no GPS underground; the library has no geographic model and never should.
6. **Blob transport** — send arbitrary bytes with a hash-verified proof of delivery. Chunked inside Text messages (validated byte-clean in firmware). See [`device/retranslator/FIRMWARE_TODO.md`](../device/retranslator/FIRMWARE_TODO.md) for open proposals to the protocol author that could simplify this later.
7. **Local archive** — the phone is the source of truth for that user's history. The library persists messages across app launches and survives the abonent's ring buffer rolling over.
8. **Gap fill on reconnect** — on BLE reconnect, compare local per-sender high-seq against the abonent's view and pull what's missing via `GetMessages`.
9. **Statistics** — counters and gauges covering BLE session health (connects, disconnects, uptime, MTU), traffic (bytes and messages in/out by type), blob transfers (chunks, hash matches, timeouts), and archive size. Persisted across launches, observable as a reactive snapshot, and clearable via a single `resetStatistics()` call. The library records; the app decides what to display or export.
10. **Diagnostic logs** — structured log entries (level, tag, timestamp, message) written to a rolling on-disk buffer with a configurable size cap. Covers the library's own internals (BLE state transitions, command framing, chunk reassembly, hash verification, gap-fill, archive operations) and exposes a sink the embedding app can write to as well. Retrievable as a stream or as a file for export/sharing, with a `clearLogs()` call. Payload bytes are never logged by default; a debug build-flag opt-in can enable verbose content logging for development only.
11. **Echo** — a debug-mode RTT probe. The library sends a small marker-prefixed `Text` payload (`ping_id`, origin timestamp) to a target peer; any tunnelchat instance that receives a marker auto-replies with a matching echo. The originator correlates by `ping_id`, computes round-trip time against its own clock, and records samples into a dedicated RTT histogram in the statistics primitive. Off by default, enabled via a debug flag, rate-limited (at most one probe per configurable interval) to avoid hogging mesh airtime. Only works between tunnelchat-using peers; bare firmware or third-party clients won't respond.

## Design philosophy

- **Phone-side smarts, firmware untouched.** Everything the library does works against the current `PROTOCOL.md` without firmware changes. Blob transport tunnels through `Text` (validated byte-clean in firmware). Any firmware-side improvements are tracked as open proposals to the protocol author, not assumptions the library relies on.
- **The mesh is the reliability layer for LoRa.** The library does not try to layer retries, acks, or CRCs over the mesh — digest sync already handles that, and duplicating it would eat airtime and ring-buffer slots. Reliability the library *does* own: BLE session robustness, local archive integrity, and UI-level delivery state derived from what the mesh tells us.
- **Delivery state is a product feature, not a transport one.** "Alice's message is shown as delivered in the UI" comes from observing that at least one peer stored the message (for blobs, that the receiver broadcast a hash-matching ACK). The library surfaces these states; the app decides how to render them.
- **Blobs are expensive; the library says so.** Every chunk is a stored mesh message competing for airtime and ring-buffer slots. The API exposes size limits and per-chunk progress so the app can show transfer status and refuse to send multi-megabyte payloads.

## Scope

**In scope:**

- BLE connection lifecycle (scan, pair, bond, reconnect, MTU negotiation, notification ordering)
- Command framing for the full PROTOCOL.md BLE command set
- Local persistent store (messages + presence timing) as source of truth
- Chunked blob transport with per-chunk + manifest hash verification
- Delivery state tracking (sent → stored locally → mesh-visible → hash-acked)
- Gap-fill on reconnect
- Persistent statistics (session, traffic, blobs, archive) with reset
- Rolling on-disk diagnostic log with retrieval, export, and clear
- Debug-mode echo probe with RTT stats, rate-limited, tunnelchat-to-tunnelchat only
- A small, stable, async public API mirrored across Android and iOS

**Out of scope:**

- UI — the library ships no views, screens, or theming
- Geographic / map semantics — node IDs are opaque bytes; their meaning belongs to the app
- End-to-end encryption (could be added later; not v1)
- LoRa-layer routing changes
- Firmware on the LoRa device (tracked in `device/retranslator/`)
- Phone-to-phone communication that bypasses the mesh (e.g. direct BLE between two phones)

## Reading order

1. This README — what and why
2. [`device/retranslator/PROTOCOL.md`](../device/retranslator/PROTOCOL.md) — the wire protocol the library speaks
3. [`device/retranslator/FIRMWARE_TODO.md`](../device/retranslator/FIRMWARE_TODO.md) — firmware changes that unlock cleaner future versions
4. `android/` — Kotlin implementation and its own README (coming next)
