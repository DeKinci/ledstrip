# MicroProto: Prototype → MVP Migration Plan

Migration plan for updating prototype implementation to match MVP spec (README.md).

## Phase 1: Wire Format Foundation

### 1.1 Update OpCode.h

**Current:**
```cpp
enum class OpCode : uint8_t {
    HELLO = 0x0,
    PROPERTY_UPDATE_SHORT = 0x1,
    PROPERTY_UPDATE_LONG = 0x2,
    SCHEMA_UPSERT = 0x3,
    SCHEMA_DELETE = 0x4,
    RPC_CALL = 0x5,
    RPC_RESPONSE = 0x6,
    ERROR = 0x7,
    PING = 0x8,
    PONG = 0x9
};

struct OpHeader { uint8_t opcode:4, flags:3, batch:1; }
```

**Target:**
```cpp
enum class OpCode : uint8_t {
    HELLO = 0x0,
    PROPERTY_UPDATE = 0x1,
    // 0x2 reserved for PROPERTY_DELTA
    SCHEMA_UPSERT = 0x3,
    SCHEMA_DELETE = 0x4,
    RPC = 0x5,
    PING = 0x6,
    ERROR = 0x7,
    RESOURCE_GET = 0x8,
    RESOURCE_PUT = 0x9,
    RESOURCE_DELETE = 0xA
};

// Header: opcode in low 4 bits, flags in high 4 bits
// Flags are opcode-specific (see spec section 1.2)
```

**Files to modify:**
- `wire/OpCode.h` - Update enum, remove OpHeader struct

### 1.2 Add propid Encoding to Buffer.h

**Add methods:**
```cpp
// Write propid (1-2 bytes)
bool writePropId(uint16_t id) {
    if (id <= 127) {
        return writeByte(id);
    } else if (id <= 32767) {
        return writeByte(0x80 | (id & 0x7F)) && writeByte(id >> 7);
    }
    return false;
}

// Read propid (1-2 bytes)
bool readPropId(uint16_t& id) {
    uint8_t b0;
    if (!readByte(b0)) return false;
    if ((b0 & 0x80) == 0) {
        id = b0;
        return true;
    }
    uint8_t b1;
    if (!readByte(b1)) return false;
    id = (b0 & 0x7F) | (b1 << 7);
    return true;
}
```

**Files to modify:**
- `wire/Buffer.h` - Add propid methods

### 1.3 Update PropertyBase ID Type

**Current:** `const uint8_t id;`
**Target:** `const uint16_t id;`

Also update registry:
```cpp
static constexpr size_t MAX_PROPERTIES = 32768;  // Or keep 256 for MVP
static std::array<PropertyBase*, MAX_PROPERTIES> byId;
```

**Decision:** Keep 256 for MVP (memory efficient on ESP32), spec supports expansion later.

**Files to modify:**
- `PropertyBase.h` - Change id type to uint16_t (wire supports 32767, internal can stay smaller)

---

## Phase 2: Message Layer Updates

### 2.1 Update Hello.h

**Current:** Separate HelloRequest/HelloResponse structs
**Target:** Single Hello with is_response flag

```cpp
// Unified Hello message
struct Hello {
    uint8_t protocolVersion;
    uint16_t maxPacketSize;

    // Request fields (is_response=0)
    uint32_t deviceId;

    // Response fields (is_response=1)
    uint32_t sessionId;
    uint32_t serverTimestamp;

    bool isResponse;
};

// Encode/decode based on is_response flag
bool encodeHello(WriteBuffer& buf, const Hello& hello);
bool decodeHello(ReadBuffer& buf, Hello& hello);
```

**Files to modify:**
- `messages/Hello.h` - Unify request/response

### 2.2 Update Error.h

**Current:** ErrorMessage struct
**Target:** Add schema_mismatch flag support

```cpp
struct ErrorMessage {
    uint16_t code;  // Changed from ErrorCode enum
    const char* message;
    uint8_t relatedOpcode;
    bool hasRelatedOpcode;
    bool schemaMismatch;  // NEW: flag from opcode flags
};

// Flag layout: bit0=schema_mismatch, bit1-3=reserved
```

**Files to modify:**
- `messages/Error.h` - Add schemaMismatch flag

### 2.3 Create RPC Handler (New)

**Create:** `protocol/RpcHandler.h`

```cpp
class RpcHandler {
public:
    using RpcCallback = MicroFunction<bool(uint8_t callId, ReadBuffer& params, WriteBuffer& result), 32>;

    void registerFunction(uint16_t functionId, RpcCallback callback);

    // Send RPC request
    void call(uint16_t functionId, WriteBuffer& params, bool needsResponse = true);

    // Handle incoming message (request or response based on flags.bit0)
    void onMessage(uint8_t clientId, uint8_t flags, ReadBuffer& buf);

private:
    std::array<RpcCallback, 32> _functions;
    std::array<PendingCall, 256> _pending;  // For tracking responses
    uint8_t _nextCallId = 0;
};

// Flag layout:
// Request: bit0=0, bit1=needs_response
// Response: bit0=1, bit1=success, bit2=has_return_value
```

**Files to create:**
- `protocol/RpcHandler.h`
- `protocol/RpcHandler.cpp`

### 2.4 Update PropertyUpdate.h

**Current:** Short/Long variants with uint8_t ID
**Target:** Single variant with propid encoding

```cpp
// Single update format
struct PropertyUpdateMsg {
    uint16_t propertyId;  // Encoded as propid on wire
    // Optional version fields (if property.level != LOCAL)
    uint32_t version;
    uint32_t sourceNodeId;
    bool hasVersioning;
    // Value follows (schema-defined encoding)
};

bool encodePropertyUpdate(WriteBuffer& buf, const PropertyBase* prop,
                          bool hasTimestamp = false, uint32_t timestamp = 0);
bool decodePropertyUpdate(ReadBuffer& buf, PropertyBase* prop);
```

**Files to modify:**
- `wire/PropertyUpdate.h` - Unified propid encoding

---

## Phase 3: New Type Support

### 3.1 Create ObjectProperty.h (New)

Heterogeneous structure with named fields.

```cpp
// Object field descriptor
struct ObjectField {
    const char* name;
    PropertyBase* property;  // Nested property with its own type/constraints
};

// ObjectProperty stores fields by schema order
template<size_t FieldCount>
class ObjectProperty : public PropertyBase {
public:
    ObjectProperty(const char* name, std::array<ObjectField, FieldCount> fields,
                   PropertyLevel level = PropertyLevel::LOCAL);

    // Field access
    PropertyBase* field(size_t index);
    PropertyBase* field(const char* name);

    // TypeId
    uint8_t getTypeId() const override { return 0x22; }

    // Encoding: values in schema order, no field names on wire
    size_t getSize() const override;
    const void* getData() const override;
    void setData(const void* data, size_t size) override;

private:
    std::array<ObjectField, FieldCount> _fields;
};
```

**Files to create:**
- `ObjectProperty.h`

### 3.2 Create VariantProperty.h (New)

Tagged union with runtime type selection.

```cpp
// Variant option descriptor
struct VariantOption {
    const char* name;
    uint8_t typeId;
    // Constraint info...
};

template<size_t OptionCount, size_t MaxValueSize>
class VariantProperty : public PropertyBase {
public:
    VariantProperty(const char* name, std::array<VariantOption, OptionCount> options,
                    PropertyLevel level = PropertyLevel::LOCAL);

    // Type selection
    uint8_t activeIndex() const { return _activeIndex; }
    bool setActiveIndex(uint8_t index);

    // Value access
    template<typename T> T* get();
    template<typename T> bool set(const T& value, uint8_t index);

    // TypeId
    uint8_t getTypeId() const override { return 0x23; }

    // Wire format: u8 type_index + value

private:
    std::array<VariantOption, OptionCount> _options;
    uint8_t _activeIndex = 0;
    std::array<uint8_t, MaxValueSize> _value;
};
```

**Files to create:**
- `VariantProperty.h`

### 3.3 Create ResourceProperty.h (New)

Header/body split with on-demand body fetching.

```cpp
template<typename Header, size_t MaxResources = 16>
class ResourceProperty : public PropertyBase {
public:
    // Implicit header fields (system-managed)
    struct ImplicitHeader {
        uint32_t id;
        uint32_t version;
        uint32_t size;
    };

    struct ResourceEntry {
        ImplicitHeader implicit;
        Header custom;
    };

    ResourceProperty(const char* name, PropertyLevel level = PropertyLevel::LOCAL);

    // Header access (auto-synced)
    size_t count() const { return _count; }
    const ResourceEntry* getEntry(uint32_t resourceId) const;

    // Body access callbacks (for on-demand fetch)
    using BodyReader = MicroFunction<bool(uint32_t resourceId, uint8_t* buf, size_t maxLen, size_t* outLen), 8>;
    using BodyWriter = MicroFunction<bool(uint32_t resourceId, const uint8_t* buf, size_t len), 8>;

    void setBodyReader(BodyReader reader) { _bodyReader = reader; }
    void setBodyWriter(BodyWriter writer) { _bodyWriter = writer; }

    // CRUD operations
    uint32_t create(const Header& header);  // Returns new ID
    bool updateHeader(uint32_t id, const Header& header);
    bool remove(uint32_t id);

    // TypeId
    uint8_t getTypeId() const override { return 0x24; }

    // Wire format (headers only): varint count, then for each: id, version, size, custom header

private:
    std::array<ResourceEntry, MaxResources> _entries;
    size_t _count = 0;
    uint32_t _nextId = 1;
    BodyReader _bodyReader;
    BodyWriter _bodyWriter;
};
```

**Files to create:**
- `ResourceProperty.h`
- `resource/ResourceStorage.h` - SPIFFS backend for bodies

---

## Phase 4: Protocol Handler Updates

### 4.1 Create Resource Handlers (New)

Handle RESOURCE_GET/PUT/DELETE opcodes.

```cpp
// protocol/ResourceHandler.h
class ResourceHandler {
public:
    void onResourceGet(uint8_t clientId, uint8_t requestId,
                       uint16_t propertyId, uint32_t resourceId);
    void onResourcePut(uint8_t clientId, uint8_t requestId, uint8_t flags,
                       uint16_t propertyId, uint32_t resourceId,
                       ReadBuffer& data);
    void onResourceDelete(uint8_t clientId, uint8_t requestId,
                          uint16_t propertyId, uint32_t resourceId);
};
```

**Files to create:**
- `protocol/ResourceHandler.h`
- `protocol/ResourceHandler.cpp`

### 4.2 Update MessageRouter

Add routing for new opcodes.

**Add to MessageHandler interface:**
```cpp
virtual void onRpc(uint8_t flags, ReadBuffer& buf) {}
virtual void onPing(bool isResponse, uint32_t payload) {}
virtual void onResourceGet(...) {}
virtual void onResourcePut(...) {}
virtual void onResourceDelete(...) {}
```

**Files to modify:**
- `messages/MessageRouter.h`
- `messages/MessageRouter.cpp`

### 4.3 Update MicroProtoServer

Integrate new message types and handlers.

**Changes:**
- Remove _clientReady tracking (move to ProtocolHandler)
- Add RpcHandler integration
- Add ResourceHandler integration
- Update broadcast logic for propid encoding

**Files to modify:**
- `transport/MicroProtoServer.h`
- `transport/MicroProtoServer.cpp`

---

## Phase 5: Schema Encoding Updates

### 5.1 Update TypeCodec

Add encoding for OBJECT, VARIANT, RESOURCE types.

```cpp
// New encode/decode methods
bool encodeObject(WriteBuffer& buf, const ObjectProperty* prop);
bool decodeObject(ReadBuffer& buf, ObjectProperty* prop);

bool encodeVariant(WriteBuffer& buf, const VariantProperty* prop);
bool decodeVariant(ReadBuffer& buf, VariantProperty* prop);

bool encodeResource(WriteBuffer& buf, const ResourceProperty* prop);
bool decodeResource(ReadBuffer& buf, ResourceProperty* prop);
```

**Files to modify:**
- `wire/TypeCodec.h`
- `wire/TypeCodec.cpp`

### 5.2 Update Schema.cpp

Add schema encoding for new types.

**For OBJECT:**
```
type_id: 0x22
varint field_count
for each field:
    ident field_name
    DATA_TYPE_DEFINITION (recursive)
```

**For VARIANT:**
```
type_id: 0x23
varint type_count
for each option:
    utf8 name
    DATA_TYPE_DEFINITION
```

**For RESOURCE:**
```
type_id: 0x24
DATA_TYPE_DEFINITION (header type)
DATA_TYPE_DEFINITION (body type)
```

**Files to modify:**
- `messages/Schema.h`
- `messages/Schema.cpp`

---

## Phase 6: Storage Updates

### 6.1 Update PropertyStorage

Support for propid-based keys.

**Files to modify:**
- `PropertyStorage.h`
- `PropertyStorage.cpp`

### 6.2 Create ResourceStorage (New)

SPIFFS backend for resource bodies.

```cpp
// storage/ResourceStorage.h
class ResourceStorage {
public:
    // Path: /res/{propertyName}/{resourceId}.bin
    static bool exists(uint16_t propertyId, uint32_t resourceId);
    static size_t size(uint16_t propertyId, uint32_t resourceId);
    static bool read(uint16_t propertyId, uint32_t resourceId,
                     size_t offset, uint8_t* buf, size_t maxLen, size_t* outLen);
    static bool write(uint16_t propertyId, uint32_t resourceId,
                      size_t offset, const uint8_t* buf, size_t len);
    static bool remove(uint16_t propertyId, uint32_t resourceId);
};
```

**Files to create:**
- `storage/ResourceStorage.h`
- `storage/ResourceStorage.cpp`

---

## Phase 7: Testing & Cleanup

### 7.1 Update Native Tests

Update existing tests for new wire format.

**Test files to update:**
- `test/native/test_wire/` - Buffer, TypeCodec tests
- `test/native/test_messages/` - Hello, Error, Schema tests

**New test files:**
- `test/native/test_rpc/`
- `test/native/test_resource/`
- `test/native/test_object/`
- `test/native/test_variant/`

### 7.2 Update Integration Tests

Python tests for protocol flow.

**Files to update:**
- `test/integration/test_microproto.py`

### 7.3 Cleanup

Remove obsolete code:
- Remove PROPERTY_UPDATE_SHORT/LONG distinction
- Remove RPC_CALL/RPC_RESPONSE split
- Remove PING/PONG split
- Remove old OpHeader struct

---

## Implementation Order

Recommended order for incremental development:

1. **Wire format** (Phase 1) - Foundation for everything else
2. **Message updates** (Phase 2.1-2.2) - Hello, Error
3. **PropertyUpdate** (Phase 2.4) - Most frequent message
4. **MessageRouter** (Phase 4.2) - Routing infrastructure
5. **RPC** (Phase 2.3, 4.2) - Function calls
6. **OBJECT type** (Phase 3.1, 5.1, 5.2) - First composite type
7. **VARIANT type** (Phase 3.2, 5.1, 5.2) - Second composite type
8. **RESOURCE type** (Phase 3.3, 4.1, 5.1, 5.2, 6.2) - Most complex
9. **Testing** (Phase 7) - Validate everything

---

## File Summary

### New Files
- `ObjectProperty.h`
- `VariantProperty.h`
- `ResourceProperty.h`
- `protocol/RpcHandler.h`
- `protocol/RpcHandler.cpp`
- `protocol/ResourceHandler.h`
- `protocol/ResourceHandler.cpp`
- `storage/ResourceStorage.h`
- `storage/ResourceStorage.cpp`

### Modified Files
- `wire/OpCode.h`
- `wire/Buffer.h`
- `wire/TypeCodec.h`
- `wire/TypeCodec.cpp`
- `wire/PropertyUpdate.h`
- `PropertyBase.h`
- `messages/Hello.h`
- `messages/Error.h`
- `messages/Schema.h`
- `messages/Schema.cpp`
- `messages/MessageRouter.h`
- `messages/MessageRouter.cpp`
- `transport/MicroProtoServer.h`
- `transport/MicroProtoServer.cpp`

### Files to Remove/Deprecate
- None (refactor in place)

---

## Notes

- **No backward compatibility**: Prototype clients won't work with MVP server
- **BLE deferred**: Full schema sync on BLE, but implement after WebSocket works
- **LOCAL only for MVP**: Skip GROUP/GLOBAL versioning logic (keep Level enum for schema)
- **RPC timeout**: 60 seconds
- **Property ID limit**: Keep internal limit at 256 for MVP, wire supports 32767