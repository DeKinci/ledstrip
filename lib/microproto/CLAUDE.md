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
├── Property.h              # Property<T> - single values
├── ArrayProperty.h         # ArrayProperty<T,N> - fixed-size arrays
├── ListProperty.h          # ListProperty<T,N> - variable-size lists
├── PropertyBase.h/.cpp     # Base class, registry, UI hints, constraints
├── PropertySystem.h/.cpp   # DirtySet, flush callbacks, change dispatch
├── PropertyStorage.h/.cpp  # NVS persistence
├── TypeTraits.h            # Type IDs and size traits
├── Logger.h                # Logging macros
│
├── wire/                   # Binary encoding
│   ├── Buffer.h            # ReadBuffer/WriteBuffer
│   ├── OpCode.h            # Protocol opcodes
│   ├── PropertyUpdate.h    # Property value encoding
│   └── TypeCodec.h/.cpp    # Type-specific encode/decode
│
├── messages/               # Protocol messages
│   ├── Hello.h             # Handshake message
│   ├── Error.h             # Error codes and messages
│   ├── Schema.h/.cpp       # Schema serialization
│   └── MessageRouter.h/.cpp # Opcode dispatch
│
└── transport/              # Network layer
    └── MicroProtoServer.h/.cpp  # WebSocket server (coupled)
```

## Key Classes

### PropertyBase (PropertyBase.h)
- Abstract base for all properties
- Maintains static registry of all properties (`_registry`)
- Provides type-erased interface: `encodeValue()`, `decodeValue()`, `encodeSchema()`
- Contains UI hints: `Widget`, `UIColor`, min/max constraints

### Property<T> (Property.h)
- Template for single-value properties
- Example: `Property<uint8_t> brightness{0, "brightness", 0, 255}`
- Tracks dirty state, calls change callbacks

### PropertySystem (PropertySystem.h)
- `DirtySet` - bitset tracking which properties changed
- `PropertySystem::onFlush()` - register callback for dirty properties
- Debounced NVS persistence (5 second delay after changes)

### MicroProtoServer (transport/MicroProtoServer.h)
- WebSocket server handling protocol
- **Currently couples transport + protocol logic** (to be separated)
- Handles HELLO handshake, schema sync, value updates, broadcasting

### MessageRouter (messages/MessageRouter.h)
- Dispatches incoming messages by opcode
- Calls registered handlers: `MessageHandler::onHello()`, `onPropertyUpdate()`, etc.

## Protocol Flow

1. Client connects via WebSocket
2. Server sends HELLO with version + property count
3. Client responds with HELLO (capabilities)
4. Server sends SCHEMA_UPSERT for each property
5. Server sends PROPERTY_UPDATE for each property value
6. Bidirectional updates flow as properties change

## Important Patterns

### Creating Properties
```cpp
// In header
Property<uint8_t> brightness{0, "brightness", 0, 255};  // id, name, min, max
Property<String> mode{"default", "mode"};
ArrayProperty<uint8_t, 3> color{{255,255,255}, "color"}; // RGB
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

## Current Limitations (Prototype Code)

1. **Transport coupling** - MicroProtoServer mixes WebSocket code with protocol logic
2. **No JSON encoding** - Only binary format supported
3. **No resources** - Only small properties, no blob/file support
4. **Single transport** - WebSocket only, no HTTP REST or BLE
5. **Property ID is uint8_t** - Spec allows propid (0-32767), code limited to 256

See [Implementation.md](Implementation.md) for target architecture addressing these.