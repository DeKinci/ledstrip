# MicroProto - Overview for Claude

Quick reference for understanding the MicroProto codebase.

## What is MicroProto?

A binary property-based protocol for embedded systems. Properties are typed values (int, float, string, color, arrays) that:
- Sync bidirectionally between device and clients via WebSocket
- Have schema metadata for auto-generated UIs (min/max, widgets, names)
- Persist to NVS flash storage
- Track dirty state for efficient delta updates

## Current File Structure

```
lib/microproto/
├── MicroProtoController.h/.cpp  # Protocol engine (transport-agnostic)
├── Property.h              # Property<T> - single values
├── ArrayProperty.h         # ArrayProperty<T,N> - fixed-size arrays
├── ListProperty.h          # ListProperty<T,N> - variable-size lists
├── ObjectProperty.h        # ObjectProperty<T> - struct properties
├── VariantProperty.h       # VariantProperty + MicroVariant<N> nestable type
├── ResourceProperty.h      # ResourceProperty - header/body split (large data)
├── PropertyBase.h/.cpp     # Base class, registry, UI hints, constraints
├── PropertySystem.h/.cpp   # DirtySet, flush callbacks, change dispatch
├── PropertyStorage.h/.cpp  # NVS persistence (name-based FNV hash keys)
├── TypeTraits.h            # Type IDs and size traits (incl. INT16/UINT16)
├── Reflect.h               # Compile-time struct reflection + field names
│
├── wire/                   # Binary encoding
│   ├── Buffer.h            # ReadBuffer/WriteBuffer
│   ├── OpCode.h            # Protocol opcodes + flags
│   ├── PropertyUpdate.h    # Property value encoding
│   └── TypeCodec.h/.cpp    # Type-specific encode/decode (incl. OBJECT elements)
│
├── messages/               # Protocol messages
│   ├── Hello.h             # Handshake (incl. idle flag for gateway)
│   ├── Error.h             # Error codes and messages
│   ├── Schema.h/.cpp       # Schema serialization
│   └── MessageRouter.h/.cpp # Opcode dispatch → MessageHandler interface
│
└── transport/              # Transport layer (thin wrappers)
    ├── MicroProtoTransport.h    # Abstract transport interface
    ├── MicroProtoServer.h/.cpp  # WebSocket transport
    ├── MicroProtoBleServer.h/.cpp # BLE transport (fragmentation, reassembly)
    └── BleFragmentation.h       # BLE fragment/reassemble helpers
```

## Key Classes

### MicroProtoController (MicroProtoController.h)
- **Protocol engine** — all protocol logic lives here, transport-agnostic
- Implements `MessageHandler`: onHello, onPropertyUpdate, onResourceGet/Put/Delete, onRpc, onPing
- Implements `FlushListener`: broadcasts dirty properties to all ready clients
- Manages multiple transports via `registerTransport()`
- Routes messages to correct transport via global client IDs

### MicroProtoTransport (transport/MicroProtoTransport.h)
- Abstract interface: `send()`, `maxClients()`, `isClientConnected()`, `maxPacketSize()`, `capabilities()`
- Implemented by: WebSocket (MicroProtoServer), BLE (MicroProtoBleServer), Gateway (GatewayTransport)
- BLE transport returns `capabilities().requiresBleExposed = true` for property filtering

### PropertyBase (PropertyBase.h)
- Abstract base for all properties
- Static registry: `byId[]` array (max 256)
- UI hints: Widget types (per-property-type), UIColor, min/max constraints
- Named constants: `PB::PERSISTENT`, `PB::READONLY`, `PB::HIDDEN`
- `readonly` blocks `setData()` (client writes) but NOT internal mutation methods

### Property Types
- `Property<T>` — single values (bool, uint8_t, int8_t, int16_t, uint16_t, int32_t, float)
- `ArrayProperty<T,N>` — fixed-size arrays
- `ListProperty<T,N>` — variable-size lists (supports OBJECT elements)
- `ObjectProperty<T>` — structs with `MICROPROTO_FIELD_NAMES` for named fields
- `VariantProperty<N,M>` — tagged unions; `MicroVariant<N>` for nestable variant values
- `ResourceProperty<N,M>` — header/body split for large data (shaders)

### PropertySystem (PropertySystem.h)
- `DirtySet` — bitset tracking which properties changed
- `FlushListener` — interface for broadcast callbacks (controller is sole listener)
- Debounced NVS persistence

### MessageRouter (messages/MessageRouter.h)
- Dispatches incoming binary messages by opcode
- Calls `MessageHandler` methods (implemented by MicroProtoController)

## Protocol Flow

1. Client connects via transport (WebSocket, BLE, or Gateway)
2. Client sends HELLO (with optional idle flag for gateway registration)
3. Controller responds with HELLO + schema version
4. Controller sends SCHEMA_UPSERT (batched, skipped if schema version matches)
5. Controller sends PROPERTY_UPDATE (batched, all current values)
6. Bidirectional updates flow as properties change
7. Gateway: idle HELLO deactivates client (stops broadcasts)

## Important Patterns

### Creating Properties
```cpp
using PB = MicroProto::PropertyBase;

// Simple property with constraints and UI hints
MicroProto::Property<uint8_t> brightness("brightness", 255, MicroProto::PropertyLevel::LOCAL,
    MicroProto::Constraints<uint8_t>().min(0).max(255),
    "LED brightness",
    MicroProto::UIHints().setColor(MicroProto::UIColor::AMBER).setIcon("💡").setUnit("%"),
    PB::PERSISTENT);

// Float with widget
MicroProto::Property<float> speed("speed", 1.0f, MicroProto::PropertyLevel::LOCAL,
    MicroProto::Constraints<float>().min(0.0f).max(2.0f).step(0.01f),
    "Speed multiplier",
    MicroProto::UIHints().setColor(MicroProto::UIColor::SKY).setWidget(MicroProto::Widget::Decimal::SLIDER),
    PB::PERSISTENT);

// Readonly hidden list (device writes, client reads)
MicroProto::ListProperty<uint8_t, 600> ledPreview("ledPreview", {},
    MicroProto::PropertyLevel::LOCAL,
    "Live LED RGB", MicroProto::UIHints(),
    PB::NOT_PERSISTENT, PB::READONLY, PB::HIDDEN);

// LIST of OBJECT with field names
struct SegmentData { std::array<uint8_t, 8> name; uint16_t ledCount; int16_t x, y; ... };
MICROPROTO_FIELD_NAMES(SegmentData, "name", "ledCount", "x", "y", ...);
MicroProto::ListProperty<SegmentData, 12> segments("segments", ...);
```

### Dirty Tracking
```cpp
brightness.set(128);  // Marks dirty automatically
// PropertySystem::flush() broadcasts to connected clients
```

### Change Callbacks
```cpp
brightness.onChange([](uint8_t newValue) {
    // Called when value changes (from local set OR remote update)
});
```

## Development Status

**Spec**: MVP draft complete (README.md) - ready for implementation
**Code**: Prototype - needs updating to match MVP spec

### MVP Scope (implement now)

- WebSocket transport (BLE added later in MVP phase)
- All property types: BOOL, INT8, UINT8, INT32, FLOAT32, ARRAY, LIST, OBJECT, VARIANT, RESOURCE
- HELLO handshake with full schema sync
- PROPERTY_UPDATE (single + batched)
- SCHEMA_UPSERT/DELETE
- RPC (request + response)
- RESOURCE_GET/PUT/DELETE
- PING/ERROR
- All properties as LOCAL level (no versioning)

### Deferred (spec is ready, implement later)

- **Mesh/distributed**: GROUP/GLOBAL levels, versioning, source_node_id, ESP-NOW/MESH
- **Schema caching**: Send schema hash, skip if client has it
- **Delta updates**: PROPERTY_DELTA (opcode 0x2 reserved)
- **Selective sync**: Only sync subset of properties

### Key Spec Changes (prototype → MVP)

- Unified 4-bit flags per opcode (batch flag moved into per-opcode flags)
- Single PROPERTY_UPDATE with propid encoding (removed SHORT/LONG split)
- Single RPC opcode with is_response flag (merged CALL/RESPONSE)
- Single PING opcode with is_response flag (merged PING/PONG)
- propid encoding: 1 byte for IDs 0-127, 2 bytes for 128-32767
- varint for all variable-length integers (sizes, timestamps, etc.)
- Added OBJECT, VARIANT, RESOURCE types
- Added RESOURCE_GET/PUT/DELETE opcodes
- ERROR has schema_mismatch flag for resync signaling
- RPC timeout: 60 seconds

See [todo.md](todo.md) for future considerations.

## Current Limitations

1. **No JSON encoding** — only binary format supported
2. **Property ID is uint8_t** — spec allows propid (0-32767), code limited to 256
3. **No HTTP REST transport** — properties only accessible via WebSocket/BLE
4. **No delta updates** — PROPERTY_DELTA (opcode 0x2) reserved but not implemented
5. **No GROUP/GLOBAL levels** — all properties are LOCAL (no cross-device sync yet)