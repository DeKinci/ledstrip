# MicroProto Implementation Architecture

Target C++ implementation architecture for the MicroProto protocol.

## Layer Structure

```
┌─────────────────────────────────────────────────────────────────────┐
│                         SCHEMA LAYER                                │
│   - Type definitions (TypeTraits)                                   │
│   - Constraints (min/max, length limits)                            │
│   - UI hints (widgets, colors, labels)                              │
└─────────────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│                         PROPERTY LAYER                              │
├─────────────────────────────────────────────────────────────────────┤
│  Regular Properties              │  Resource Properties (0x24)      │
│  ─────────────────               │  ────────────────────────────    │
│  - Property<T>                   │  - ResourceProperty<H,B>         │
│  - ArrayProperty<T,N>            │  - Headers: auto-sync (NVS)      │
│  - ListProperty<T,N>             │  - Bodies: on-demand (SPIFFS)    │
│  - Dirty tracking                │  - CRUD via RESOURCE_* opcodes   │
└─────────────────────────────────────────────────────────────────────┘
                                │
        ┌───────────────────────┼───────────────────────┐
        ▼                       ▼                       ▼
┌───────────────┐       ┌───────────────┐       ┌───────────────┐
│    PERSIST    │       │   TRANSFER    │       │    MATTER     │
├───────────────┤       ├───────────────┤       ├───────────────┤
│ - Encoding    │       │ - Encoding    │       │ - Property    │
│ - Handling    │       │   (binary,    │       │   mapping     │
│   (debounce,  │       │    json)      │       │ - Smart home  │
│    batching)  │       │ - Handling    │       │   integration │
│               │       │   (batching,  │       │               │
│               │       │    clients)   │       │               │
└───────┬───────┘       └───────┬───────┘       └───────────────┘
        │                       │
        ▼                       ▼
┌───────────────┐       ┌───────────────┐
│    STORAGE    │       │   TRANSPORT   │
├───────────────┤       ├───────────────┤
│ - NVS (props  │       │ - WebSocket   │
│   + headers)  │       │ - HTTP/REST   │
│ - SPIFFS      │       │ - BLE         │
│   (bodies)    │       │               │
└───────────────┘       └───────────────┘
```

## Directory Structure

```
lib/microproto/
├── schema/                      # SCHEMA LAYER
│   ├── TypeTraits.h             # Type IDs, sizes, Level enum
│   ├── Constraints.h            # Value/Container constraints
│   └── UIHints.h                # UI rendering hints
│
├── property/                    # PROPERTY LAYER
│   ├── PropertyBase.h           # Base class, registry, virtuals
│   ├── Property.h               # Single-value properties
│   ├── ArrayProperty.h          # Fixed-size arrays
│   ├── ListProperty.h           # Variable-size lists
│   ├── DirtySet.h               # Bitset for dirty tracking
│   └── PropertySystem.h/.cpp    # Change dispatch, flush callbacks
│
├── resource/                    # RESOURCE LAYER
│   ├── ResourceProperty.h       # RESOURCE type property (header/body split)
│   ├── ResourceHeader.h         # Header struct with custom fields
│   ├── ResourceBody.h           # Body reader/writer for SPIFFS blobs
│   └── ResourceSchema.h         # Header + body schema definitions
│
├── storage/                     # PERSIST + STORAGE LAYER
│   ├── PropertyStorage.h/.cpp   # NVS for properties (encoding, debounce)
│   ├── ResourceStorage.h/.cpp   # SPIFFS for resources
│   ├── NvsStorage.h             # NVS backend
│   └── SpiffsStorage.h          # SPIFFS backend
│
├── encoding/                    # TRANSFER ENCODING
│   ├── Buffer.h                 # Read/Write buffers
│   ├── BinaryCodec.h            # Binary encode/decode
│   ├── JsonCodec.h              # JSON encode/decode
│   └── SchemaCodec.h            # Schema serialization
│
├── protocol/                    # TRANSFER HANDLING
│   ├── ProtocolHandler.h        # Client state, batching
│   ├── MessageRouter.h          # Opcode dispatch
│   ├── OpCode.h                 # Protocol constants
│   └── Messages.h               # Hello, Error, PropertyUpdate
│
├── transport/                   # TRANSPORT BACKENDS
│   ├── Transport.h              # Abstract interface
│   ├── WebSocketTransport.h     # WebSocket implementation
│   ├── HttpTransport.h          # REST adapter
│   └── BleTransport.h           # BLE implementation
│
└── matter/                      # MATTER LAYER
    └── TODO                     # TBD
```

## Key Interfaces

### Property Layer

```cpp
// schema/TypeTraits.h - Level enum for schema-driven versioning
enum class Level : uint8_t {
    LOCAL = 0,   // No version/source_node_id in PROPERTY_UPDATE
    GROUP = 1,   // Version/source_node_id included for conflict resolution
    GLOBAL = 2   // Version/source_node_id included for conflict resolution
};

// property/PropertyBase.h - Value + schema metadata
class PropertyBase {
public:
    uint16_t id() const;         // Property ID (0-32767, encoded as propid on wire)
    const char* name() const;
    TypeId typeId() const;
    Level level() const;         // LOCAL, GROUP, or GLOBAL (for schema-driven versioning)

    // Type-erased value access
    virtual const void* rawValue() const = 0;
    virtual void setRawValue(const void* data, size_t len) = 0;
    virtual size_t valueSize() const = 0;

    // Schema metadata (mutable at runtime)
    Constraints& constraints();
    UIHints& uiHints();

    // Dirty flags
    bool isValueDirty() const;
    bool isSchemaDirty() const;
    void markValueDirty();
    void markSchemaDirty();
};

// property/PropertySystem.h - Tracks value AND schema changes
class PropertySystem {
public:
    using ChangeCallback = MicroFunction<void(const DirtySet& values, const DirtySet& schemas), 16>;

    static void markValueDirty(uint16_t id);
    static void markSchemaDirty(uint16_t id);
    static int8_t onChange(ChangeCallback callback);  // Gets both dirty sets
    static void loop();  // Notify subscribers
};
```

### Resource Layer

Resources use the RESOURCE property type (0x24) with a header/body split:
- **Headers**: Custom metadata fields, auto-synced via PROPERTY_UPDATE, stored in NVS (per-resource)
- **Bodies**: Large structured data, fetched on-demand via RESOURCE_GET, stored in SPIFFS

```cpp
// resource/ResourceHeader.h - Header with implicit + custom fields
template<typename CustomHeader>
struct ResourceHeader {
    // Implicit fields (system-managed)
    uint32_t id;          // Unique ID, assigned by server
    uint32_t version;     // Increments on body update
    uint32_t size;        // Body size in bytes

    // Custom fields (defined by user)
    CustomHeader custom;  // e.g., { name, author, enabled }
};

// Example custom header
struct ShaderHeader {
    String name;
    String author;
    bool enabled;
};

// resource/ResourceProperty.h - RESOURCE type property
template<typename Header, typename Body, size_t MaxResources = 16>
class ResourceProperty : public PropertyBase {
public:
    ResourceProperty(uint16_t id, const char* name);

    // Header access (auto-synced)
    size_t count() const;
    const ResourceHeader<Header>* getHeader(uint32_t resourceId) const;
    bool headerExists(uint32_t resourceId) const;

    // Body access (on-demand)
    bool readBody(uint32_t resourceId, Body& outBody);

    // Modification (triggers header broadcast)
    uint32_t create(const Header& header, const Body& body);
    bool update(uint32_t resourceId, const Header& header, const Body& body);
    bool remove(uint32_t resourceId);

    // Schema metadata
    static constexpr TypeId TYPE_ID = TypeId::RESOURCE;
    const FieldSchema* headerSchema() const;
    const FieldSchema* bodySchema() const;

private:
    std::array<ResourceHeader<Header>, MaxResources> _headers;
    size_t _count = 0;
};

// resource/ResourceBody.h - Chunked I/O for bodies
class ResourceBodyReader {
public:
    ResourceBodyReader(uint16_t propertyId, uint32_t resourceId);
    bool open();
    size_t read(uint8_t* buf, size_t maxLen);
    size_t totalSize() const;
    bool eof() const;
    void close();
};

class ResourceBodyWriter {
public:
    ResourceBodyWriter(uint16_t propertyId, uint32_t resourceId);
    bool begin(size_t totalSize);
    bool write(const uint8_t* buf, size_t len);
    bool commit();  // Updates header.version and header.size, broadcasts
    void abort();
};

// resource/ResourceSchema.h - Schema definitions for header + body
struct FieldSchema {
    const char* name;
    TypeId type;
    // ... constraints, nested definitions
};

template<typename Header, typename Body>
class ResourceSchema {
public:
    static const FieldSchema* headerFields();
    static size_t headerFieldCount();
    static const FieldSchema* bodyFields();
    static size_t bodyFieldCount();
};
```

### Resource Storage

```cpp
// storage/ResourceHeaderStorage.h - NVS for headers (per-resource entries)
class ResourceHeaderStorage {
public:
    // Key format: "{propertyName}:{resourceId}" e.g., "shaders:1"
    static bool load(uint16_t propertyId, uint32_t resourceId, void* header, size_t size);
    static bool save(uint16_t propertyId, uint32_t resourceId, const void* header, size_t size);
    static bool remove(uint16_t propertyId, uint32_t resourceId);
    static bool listIds(uint16_t propertyId, uint32_t* ids, size_t maxCount, size_t* outCount);
};

// storage/ResourceBodyStorage.h - SPIFFS for bodies
class ResourceBodyStorage {
public:
    // Path format: "/res/{propertyName}/{resourceId}.bin" e.g., "/res/shaders/1.bin"
    static bool exists(uint16_t propertyId, uint32_t resourceId);
    static size_t size(uint16_t propertyId, uint32_t resourceId);
    static bool read(uint16_t propertyId, uint32_t resourceId,
                     size_t offset, uint8_t* buf, size_t maxLen, size_t* outLen);
    static bool write(uint16_t propertyId, uint32_t resourceId,
                      size_t offset, const uint8_t* buf, size_t len);
    static bool remove(uint16_t propertyId, uint32_t resourceId);
};
```

### Resource Usage Example

```cpp
// Define header and body schemas
struct ShaderHeader {
    String name;
    String author;
    bool enabled = true;
};

struct ShaderBody {
    String code;
    float speed = 1.0f;
    std::array<uint8_t, 3> primaryColor = {255, 255, 255};
};

// Create resource property
ResourceProperty<ShaderHeader, ShaderBody, 32> shaders{1, "shaders"};

// Create new resource
ShaderHeader h{"rainbow", "me", true};
ShaderBody b{"void main() {...}", 1.5f, {255, 0, 128}};
uint32_t id = shaders.create(h, b);
// → Server broadcasts updated headers via PROPERTY_UPDATE

// Read body on-demand
ShaderBody loadedBody;
shaders.readBody(id, loadedBody);

// Delete resource
shaders.remove(id);
// → Server broadcasts updated headers via PROPERTY_UPDATE
```

### Encoding Layer

Wire format types:
- **propid**: 1-2 byte property ID (0-127 = 1 byte, 128-32767 = 2 bytes)
- **varint**: Protobuf-style variable-length integer (7 bits per byte, MSB continuation)

```cpp
// encoding/BinaryCodec.h - Binary format for WebSocket/BLE
class BinaryCodec {
public:
    // Property value encoding
    static bool encode(WriteBuffer& buf, const PropertyBase* prop);
    static bool decode(ReadBuffer& buf, PropertyBase* prop);
    static bool encodeSchema(WriteBuffer& buf, const PropertyBase* prop);

    // Wire format primitives
    static bool writePropId(WriteBuffer& buf, uint16_t id);   // 1-2 bytes
    static bool readPropId(ReadBuffer& buf, uint16_t* id);
    static bool writeVarint(WriteBuffer& buf, uint32_t val);  // 1-5 bytes
    static bool readVarint(ReadBuffer& buf, uint32_t* val);

    // Schema-driven PROPERTY_UPDATE encoding:
    // - Property ID encoded as propid
    // - If property.level() != LOCAL: version + source_node_id follow (as varint)
    // - Value bytes follow
    static bool encodePropertyUpdate(WriteBuffer& buf, const PropertyBase* prop,
                                     uint32_t version = 0, uint32_t sourceNodeId = 0);
    static bool decodePropertyUpdate(ReadBuffer& buf, PropertyBase* prop,
                                     uint32_t* version, uint32_t* sourceNodeId);
};

// encoding/JsonCodec.h - JSON format for HTTP REST
class JsonCodec {
public:
    static bool encode(JsonObject& obj, const PropertyBase* prop);
    static bool decode(const JsonObject& obj, PropertyBase* prop);
    static bool encodeSchema(JsonObject& obj, const PropertyBase* prop);
};
```

### Storage Layer

```cpp
// storage/PropertyStorage.h - NVS persistence
class PropertyStorage {
public:
    static void load(PropertyBase* prop);
    static void save(PropertyBase* prop);
};

// storage/ResourceStorage.h - SPIFFS for blobs
class ResourceStorage {
public:
    static bool read(uint16_t id, uint8_t* buf, size_t maxLen, size_t* outLen);
    static bool write(uint16_t id, const uint8_t* buf, size_t len);
    static bool remove(uint16_t id);
    static bool list(uint16_t* ids, size_t maxCount, size_t* outCount);
};
```

### Protocol Layer

```cpp
// protocol/OpCode.h - Operation header format
//
// Every message starts with: u8 { opcode: bit4, flags: bit4 }
//
// Flags are opcode-specific:
//   PROPERTY_UPDATE (0x1): bit0=batch, bit1=has_timestamp
//   SCHEMA_UPSERT (0x3):   bit0=batch
//   SCHEMA_DELETE (0x4):   bit0=batch
//   RPC Request (0x5):     bit0=0, bit1=needs_response
//   RPC Response (0x5):    bit0=1, bit1=success, bit2=has_return_value
//   HELLO/PING (0x0/0x6):  bit0=is_response
//   RESOURCE_GET (0x8):    bit0=is_response; Response: bit1=status
//   RESOURCE_PUT (0x9):    Request: bit0=0, bit1=update_header, bit2=update_body
//                          Response: bit0=1, bit1=status
//   RESOURCE_DELETE (0xA): bit0=is_response; Response: bit1=status

enum class OpCode : uint8_t {
    HELLO = 0x0,
    PROPERTY_UPDATE = 0x1,
    SCHEMA_UPSERT = 0x3,
    SCHEMA_DELETE = 0x4,
    RPC = 0x5,
    PING = 0x6,
    ERROR = 0x7,
    RESOURCE_GET = 0x8,
    RESOURCE_PUT = 0x9,
    RESOURCE_DELETE = 0xA
};

// protocol/ProtocolHandler.h - Message handling, uses BinaryCodec
class ProtocolHandler {
public:
    ProtocolHandler(Transport* transport);

    void onMessage(uint8_t clientId, const uint8_t* data, size_t len);
    void loop();  // Flush broadcasts
};

// protocol/RpcHandler.h - RPC request/response handling
class RpcHandler {
public:
    using RpcCallback = MicroFunction<bool(uint8_t callId, ReadBuffer& params, WriteBuffer& result), 32>;

    // Register function handler
    void registerFunction(uint16_t functionId, RpcCallback callback);

    // Send RPC request
    // If needsResponse=false, callId is not sent (fire-and-forget)
    void call(uint16_t functionId, WriteBuffer& params, bool needsResponse = true);

    // Handle incoming RPC message (request or response based on flags.bit0)
    void onMessage(uint8_t clientId, uint8_t flags, ReadBuffer& buf);
};
```

### Transport Layer

```cpp
// transport/Transport.h - Abstract send/receive
class Transport {
public:
    virtual void send(uint8_t clientId, const uint8_t* data, size_t len) = 0;
    virtual void broadcast(const uint8_t* data, size_t len) = 0;
};

// Implementations: WebSocketTransport, HttpTransport, BleTransport
```

## Design Principles

1. **Properties are pure values** - No encoding logic, just data + metadata
2. **Encoding is separate** - Codecs know how to serialize, properties don't
3. **Layered architecture** - Schema → Property/Resource → Persist/Transfer/Matter
4. **Subscribers handle their own concerns** - Persist debounces, Transfer batches
5. **Transport is just send/receive** - Protocol layer handles message semantics
6. **Schema-driven versioning** - Receiver uses property's `level` to determine if version/source_node_id are present in PROPERTY_UPDATE; no per-property flags needed on the wire