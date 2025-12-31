# MicroProto Protocol Specification v1.0

> **See also:** [Implementation.md](Implementation.md) for target architecture and refactoring roadmap.

A binary, space-efficient property-based protocol for distributed embedded systems with support for properties, RPC, and dynamic schema management.

## Design Goals
- Binary and space-efficient for resource-constrained devices (ESP32, etc.)
- Separation of schema definition and data transmission
- Support for multiple transports (WebSocket, BLE, ESP-NOW, ESP-MESH)
- Dynamic schema updates without protocol changes
- Efficient batching for reduced overhead
- Self-describing for auto-generated UIs

## Protocol Version
- Version: 1
- First message after connection must be HELLO

---

## 1. Core Structure

### 1.1 Message Envelope
Every message starts with an operation header:

```
u8 operation_header {
    opcode: bit4,        // Operation type (0-15)
    flags: bit4          // Operation-specific flags (see each opcode)
}
```

### 1.2 Operation Codes (opcodes)

| Opcode | Name | Direction | Description |
|--------|------|-----------|-------------|
| 0x0 | HELLO | Bidirectional | Protocol handshake, version check |
| 0x1 | PROPERTY_UPDATE | Bidirectional | Property value update |
| 0x2 | RESERVED | - | Reserved for PROPERTY_DELTA (partial/delta updates) |
| 0x3 | SCHEMA_UPSERT | Server→Client | Create or update schema definition |
| 0x4 | SCHEMA_DELETE | Server→Client | Delete schema definition |
| 0x5 | RPC | Bidirectional | Remote procedure call |
| 0x6 | PING | Bidirectional | Heartbeat |
| 0x7 | ERROR | Bidirectional | Error message |
| 0x8 | RESOURCE_GET | Bidirectional | Get resource body |
| 0x9 | RESOURCE_PUT | Bidirectional | Create/update resource |
| 0xA | RESOURCE_DELETE | Bidirectional | Delete resource |
| 0xB-0xF | RESERVED | - | Reserved for future use |

---

## 2. Connection Lifecycle

### 2.1 HELLO (opcode 0x0)

HELLO serves dual purposes:
- **Initial connection**: Exchange protocol version; mismatch causes ERROR + close
- **Resynchronization**: Client can send HELLO at any time to request full state reset

**Flags**:
```
bit0: is_response    // 0=request (client→server), 1=response (server→client)
bit1-3: reserved
```

### 2.2 Connection Establishment

When a client connects or needs to resync:

1. **Client sends HELLO** (flags: 0x0):
```
u8 operation_header { opcode: 0x0, flags: 0x0 }
u8 protocol_version        // Protocol version (current: 1)
varint max_packet_size     // Maximum packet size client can receive
varint device_id           // Unique device identifier
```

**Semantics**: "I am (re)connecting. Please send me complete state."

2. **Server responds with HELLO** (flags: 0x1):
```
u8 operation_header { opcode: 0x0, flags: 0x1 }
u8 protocol_version        // Protocol version (current: 1)
varint max_packet_size     // Maximum packet size server can receive
varint session_id          // Unique session identifier
varint server_timestamp    // Unix timestamp for sync
```

**Semantics**: "Reset your state. Complete schema and properties follow."

3. **Server sends SCHEMA_UPSERT** (batched):
   - Complete schema definition of all properties and functions
   - Client must clear any existing schema before processing

4. **Server sends PROPERTY_UPDATE** (batched):
   - Current values for all properties
   - Client now has complete synchronized state

5. **Connection ready for bidirectional communication**

### 2.3 Resynchronization During Active Connection

If client becomes out-of-sync (e.g., missed messages, unknown id, timeout, network hiccup):
- Client sends HELLO again (without disconnecting transport)
- Server treats it as fresh connection: sends HELLO → SCHEMA_UPSERT → PROPERTY_UPDATE
- Client clears local state and rebuilds from server messages

---

## 3. Type System

### 3.1 Basic Data Types

| Type ID | Name | Size | Description |
|---------|------|------|-------------|
| 0x01 | BOOL | 1 | Boolean (0=false, 1=true) |
| 0x02 | INT8 | 1 | Signed 8-bit integer |
| 0x03 | UINT8 | 1 | Unsigned 8-bit integer |
| 0x04 | INT32 | 4 | Signed 32-bit integer (little-endian) |
| 0x05 | FLOAT32 | 4 | IEEE 754 single precision float (little-endian) |
| 0x06-0x1F | RESERVED | - | Reserved for future basic types |

### 3.2 Composite Types (Containers)

Composite types contain **property definitions**, not just raw types. Each element/field can have its own constraints and validation.

| Type ID | Name | Description |
|---------|------|-------------|
| 0x20 | ARRAY | Fixed-size homogeneous sequence. Schema defines count and element type. Only values transmitted |
| 0x21 | LIST | Variable-size homogeneous sequence. Schema defines element type. Count sent with data |
| 0x22 | OBJECT | Heterogeneous structure. Schema defines field names and types. Values in schema order |
| 0x23 | VARIANT | Tagged union. Value can be one of several types. Tag index + value transmitted |
| 0x24 | RESOURCE | Header/body split. Headers auto-sync, bodies on-demand. Schema defines header type and body type (read-only) |
| 0x25-0x2F | RESERVED | Reserved for future composite types |

**Key principle**: Containers hold property definitions (with constraints), not bare types.
- `LIST(UINT8)` → List of unconstrained UINT8 properties
- `LIST(INT32{min:0,max:100})` → List of constrained INT32 properties
- `OBJECT{x:INT32{min:0}, y:INT32{min:0}}` → Object with two constrained fields

**Nesting support**: All composite types can be nested arbitrarily:
- ARRAY of OBJECT
- LIST of ARRAY
- OBJECT with LIST fields
- LIST of LIST
- etc.

### 3.3 Protocol-Internal Types

These types are used throughout the protocol for encoding but are not property value types.

#### varint
Variable-length integer using continuation bit encoding (similar to Protobuf):
- Bits 0-6: Data bits (7 bits per byte)
- Bit 7: Continuation bit (1 = more bytes follow, 0 = last byte)

```
0x00           → 0
0x7F           → 127
0x80 0x01      → 128
0xFF 0xFF 0x03 → 65535
```

Max 5 bytes for 32-bit values, 10 bytes for 64-bit values.

#### propid
Variable-length ID for properties, functions, and namespaces. Supports 0-32767 in 1-2 bytes.

```
If value <= 127:    1 byte:  [0xxxxxxx]           // High bit 0, 7-bit value
If value <= 32767:  2 bytes: [1xxxxxxx] [xxxxxxxx] // High bit 1, low 7 bits + high 8 bits
```

Examples:
- `0x00` → 0
- `0x7F` → 127
- `0x80 0x01` → 128 (low 7 bits: 0, high 8 bits: 1 → 0 + 128 = 128)
- `0xFF 0xFF` → 32767 (low 7 bits: 127, high 8 bits: 255 → 127 + 32640 = 32767)

**Usage**: property_id, function_id, namespace_id, item_id.

#### blob
Length-prefixed byte sequence.

```
varint length      // Number of bytes
bytes data[length] // Raw bytes
```

Examples:
- `0x00` → empty blob
- `0x05 0x01 0x02 0x03 0x04 0x05` → 5 bytes

#### utf8
Length-prefixed UTF-8 string. Same encoding as blob, contents are valid UTF-8.

```
varint length      // Number of bytes (not characters)
bytes data[length] // UTF-8 encoded string
```

Examples:
- `0x00` → empty string
- `0x05 "hello"` → "hello"
- `0x06 "héllo"` → "héllo" (é is 2 bytes in UTF-8)

#### ident
Length-prefixed identifier string. ASCII only, valid characters: `a-z`, `A-Z`, `0-9`, `_`. Used for field names, property names, parameter names where JSON/protocol compatibility is required.

```
u8 length          // Number of bytes (max 255)
bytes data[length] // ASCII identifier
```

Examples:
- `0x00` → empty (invalid)
- `0x04 "name"` → "name"
- `0x0B "brightness"` → "brightness"

**Usage**: property names, function names, object field names, parameter names.

### 3.4 Type Encoding Details

#### ARRAY (Fixed-Size)
Schema defines count and element property definition (with constraints). Only values transmitted.

**Schema definition**:
```
type_id: 0x20
varint element_count
// Element property definition:
  u8 element_type_id
  [validation_flags + constraints]  // For basic types
  [nested_type_definition]          // If element is composite, recursive definition
```

**Wire format** (property update):
```
bytes values[]         // Packed values only, no count/type
```

**Example**: `rgb: ARRAY[3](UINT8)`
- Schema: `0x20 0x03 0x03` (ARRAY, count=3, UINT8)
- Wire: `0xFF 0x00 0x00` (3 bytes, no overhead)

#### LIST (Variable-Size)
Count sent with every transmission. Schema defines element property definition.

**Schema definition**:
```
type_id: 0x21
// Element property definition:
  u8 element_type_id
  [validation_flags + constraints]  // For basic types
  [nested_type_definition]          // If element is composite, recursive definition
```

**Wire format** (property update):
```
varint count
bytes values[]         // Packed values
```

**Example**: `animation_names: LIST(LIST(UINT8))` (list of strings)
- Schema: `0x21 0x21 0x03` (LIST of LIST of UINT8)
- Wire: `0x02 0x07 "rainbow" 0x04 "fade"` (count=2, then each string with its count)

#### OBJECT (Fixed Structure)
Schema defines field names and field property definitions (with constraints). Only values transmitted in schema order.

**Schema definition**:
```
type_id: 0x22
varint field_count
// For each field (field property definition):
  ident field_name       // ASCII only (a-z, A-Z, 0-9, _)
  u8 field_type_id
  [validation_flags + constraints]  // For basic types
  [nested_type_definition]          // If field is composite, recursive definition
```

**Wire format** (property update):
```
bytes field_values[]   // Values only, in schema-defined order
```

**Example**: `position: OBJECT{x:INT32, y:INT32, label:LIST(UINT8)}`
- Schema: `0x22 0x03 0x01 "x" 0x04 0x01 "y" 0x04 0x05 "label" 0x21 0x03`
- Wire: `[4 bytes x][4 bytes y][varint len][label bytes]`

#### VARIANT (Tagged Union)
Value can be one of several types. Schema defines the possible types with human-readable names, wire format includes tag index.

**Schema definition**:
```
type_id: 0x23
varint type_count          // Number of possible types (2-255)
// For each possible type:
  utf8 name                // Human-readable name for this variant
  DATA_TYPE_DEFINITION     // Recursive: type with constraints
```

**Wire format** (property update):
```
u8 type_index              // Which type (0-based index into schema types)
bytes value                // Encoded according to selected type
```

**Example**: `result: VARIANT(value: UINT8, error: OBJECT{code:UINT8, msg:LIST(UINT8)})`
```
Schema:
  0x23                     // VARIANT
  0x02                     // 2 types
  0x05 "value"             // name="value" (UTF-8)
    [UINT8 def]            // type definition
  0x05 "error"             // name="error" (UTF-8)
    [OBJECT def]           // type definition

Wire (if value): `0x00 0x2A` (type_index=0, value=42)
Wire (if error): `0x01 0x04 0x09 "not found"` (type_index=1, object value)
```

**Use cases**:
- Error responses: `VARIANT(success: ResultData, error: ErrorInfo)`
- Polymorphic configs: `VARIANT(simple: SimpleConfig, advanced: AdvancedConfig)`
- Optional with type: `VARIANT(none: BOOL, some: ActualValue)`

#### RESOURCE (Header/Body Split)
Large structured data with automatic header sync and on-demand body fetching. Schema defines a single type for headers and a single type for body (typically OBJECT, but can be any container). This is a **read-only** property type - clients cannot modify via PROPERTY_UPDATE; they must use RESOURCE_PUT/DELETE operations.

**Schema definition**:
```
type_id: 0x24
DATA_TYPE_DEFINITION       // Header type (single type, can be nested container)
DATA_TYPE_DEFINITION       // Body type (single type, can be nested container)
```

**Implicit header fields** (always present, system-managed, prepended to header value):
- `id: varint` - Unique resource identifier, assigned by server
- `version: varint` - Increments on every body update
- `size: varint` - Body size in bytes

**Wire format** (property update - headers only):
```
varint count               // Number of resources
// For each resource:
  varint id                // System-managed
  varint version           // System-managed
  varint size              // System-managed (body size)
  bytes header_value       // Encoded according to header type
```

**Example**: Shader resources
```
Schema: shaders: RESOURCE {
  header_type: OBJECT {
    name: LIST(UINT8),
    author: LIST(UINT8),
    enabled: BOOL
  },
  body_type: OBJECT {
    code: LIST(UINT8),
    speed: FLOAT32 { min: 0.1, max: 10.0 },
    primary_color: ARRAY[3](UINT8)
  }
}

Wire (headers, auto-synced):
  0x02                     // 2 resources
  // Resource 1:
  0x01                     // id=1 (varint)
  0x03                     // version=3 (varint)
  0x80 0x10                // size=2048 (varint: 0x80 | (2048 & 0x7F), 2048 >> 7)
  0x07 "rainbow"           // name (from header OBJECT)
  0x02 "me"                // author
  0x01                     // enabled=true
  // Resource 2:
  0x02                     // id=2 (varint)
  0x01                     // version=1 (varint)
  0x80 0x04                // size=512 (varint)
  0x04 "fade"              // name
  0x03 "you"               // author
  0x01                     // enabled=true

Body (fetched via RESOURCE_GET, encoded as body_type OBJECT):
  0x12 "void main()..."    // code
  0x00 0x00 0xC0 0x3F      // speed = 1.5f
  0xFF 0x00 0x80           // primary_color = [255, 0, 128]
```

**Storage**:
- Headers: NVS (one entry per resource for efficient updates)
- Bodies: SPIFFS (file per resource)

### 3.5 String Encoding

UTF-8 strings are LIST of UINT8 (variable-length):

**Schema**: `0x21 0x03` (LIST of UINT8)
**Wire**: `varint length + utf8_bytes`

**Example**: `"hello"` → `0x05 "hello"` (6 bytes total)

### 3.6 Nested Composite Examples

**LIST of OBJECT** (each object field can have constraints):
```
Schema: devices: LIST(OBJECT{
  id: INT32,
  name: ARRAY[16](UINT8)
})
Wire: [varint count] + count * (4 + 16) bytes
Example (2 devices): 0x02 [4][16] [4][16]
```

**OBJECT with LIST field** (list elements can have constraints):
```
Schema: animation: OBJECT{
  name: LIST(UINT8{min:32,max:126}),  // Printable ASCII only
  speed: FLOAT32{min:0.1,max:10.0}
}
Wire: [varint name_len][name bytes][4 bytes speed]
Example: 0x07 "rainbow" [4 bytes]
```

**ARRAY of constrained integers**:
```
Schema: brightness_history: ARRAY[10](UINT8{min:0,max:255})
Wire: 10 bytes (packed values, constraints validated on receive)
```

**LIST with constrained elements**:
```
Schema: temperatures: LIST(INT32{min:-40,max:125})
Wire: [varint count][4 bytes][4 bytes]...
Validation: Each element must be -40 to 125, else ERROR
```

---

## 4. Schema Operations

Schema operations define the structure of properties and functions. They are sent once on connection and updated as needed.

### 4.1 Schema Structure Overview

**Property definition** = Identity + Data Type Definition + Metadata

```
PROPERTY:
  ├─ Identity (item_id, name, namespace, description)
  ├─ Data Type Definition (recursive, anonymous)
  ├─ Default value
  ├─ Flags (readonly, persistent, hidden)
  └─ UI hints (widget, unit, icon, color)
```

**Data Type Definition** is recursive and contains no identity:

```
DATA_TYPE_DEFINITION:
  ├─ type_id (BOOL/INT8/UINT8/INT32/FLOAT32/ARRAY/LIST/OBJECT)
  ├─ [if basic type]: constraints (min/max/step/pattern)
  └─ [if container type]: nested DATA_TYPE_DEFINITION(s)
```

### 4.2 SCHEMA_UPSERT (opcode 0x3)

Creates or updates a schema definition. Typically batched for efficiency.

**Flags**:
```
bit0: batch           // 1 = batched (batch_count follows)
bit1-3: reserved
```

```
u8 operation_header { opcode: 0x3, flags: see above }
[u8 batch_count]           // If batch=1 (count-1, so 0 means 1 item)

// For each schema item:
u8 item_type {
    type: bit4 {           // 0=namespace, 1=property, 2=function
        0x0: NAMESPACE
        0x1: PROPERTY
        0x2: FUNCTION
    }
    flags: bit3 {
        readonly: bit1     // Property cannot be changed by client
        persistent: bit1   // Value persists across restarts
        hidden: bit1       // Don't show in UI (internal use)
    }
    reserved: bit1
}

// For PROPERTY only: property level and sync flags
[if PROPERTY]:
u8 property_level_flags {
    level: bit2 {          // Property ownership level
        0x0: LOCAL         // Single node only, not propagated
        0x1: GROUP         // Shared within group, versioned
        0x2: GLOBAL        // Shared across all nodes, versioned
    }
    ble_exposed: bit1      // Expose on BLE (implies size constraints)
    reserved: bit5
}
[u8 group_id]              // Only if level=GROUP

propid item_id             // Unique identifier (1-2 bytes, see section 3.3)
propid namespace_id        // Parent namespace (0 = root)
ident name                 // Property/function name (ASCII: a-z, A-Z, 0-9, _)
utf8 description           // Human-readable description

// For PROPERTY type only:
DATA_TYPE_DEFINITION       // Recursive type definition (see 4.3)
bytes default_value        // Encoded according to data type

// UI hints (optional, for auto-generated UIs):
u8 ui_hints_flags {
    has_widget_hint: bit1
    has_unit: bit1
    reserved: bit2
    colorgroup: bit4
}
[u8 widget_hint]           // 0=auto, 1=slider, 2=toggle, 3=color_picker, 4=text_input, etc.
[varint unit_length]       // Unit string (e.g., "ms", "%", "°C")
  [bytes unit]             // ASCII

// For FUNCTION type only:
u8 param_count             // Number of parameters
// For each parameter:
  [ident param_name]       // Parameter name (ASCII)
  [DATA_TYPE_DEFINITION]   // Recursive type definition
  [bytes param_default]    // Optional default value
DATA_TYPE_DEFINITION       // Return type (or BOOL with no flags for void)
```

### 4.3 DATA_TYPE_DEFINITION (Recursive)

Anonymous, reusable type definition with constraints. No identity fields.

```
u8 type_id                 // Type from section 3

// === BASIC TYPES (0x01-0x05) ===
[if BOOL/INT8/UINT8/INT32/FLOAT32]:
  u8 validation_flags {
    has_min: bit1
    has_max: bit1
    has_step: bit1
    has_oneof: bit1        // Enum values
    has_pattern: bit1      // Regex pattern (for list of UINT8 strings)
    reserved: bit3
  }
  // Constraint values (only if corresponding flag is set):
  [bytes min_value]        // Encoded using type_id
  [bytes max_value]        // Encoded using type_id
  [bytes step_value]       // Encoded using type_id
  [varint oneof_count]     // Number of enum values
    [bytes oneof_values[]] // Each encoded using type_id
  [varint pattern_length]  // Regex pattern length
    [bytes pattern]        // ASCII regex

// === ARRAY (0x20) ===
[if ARRAY]:
  varint element_count     // Fixed array size
  DATA_TYPE_DEFINITION     // Recursive: element type with constraints

// === LIST (0x21) ===
[if LIST]:
  u8 length_constraints {
    has_min_length: bit1
    has_max_length: bit1
    has_unique: bit1       // Elements must be unique
    is_sorted: bit1        // Elements must be sorted ascending
    is_reverse_sorted: bit1 // Elements must be sorted descending
    reserved: bit3
  }
  [varint min_length]      // Minimum list length
  [varint max_length]      // Maximum list length
  DATA_TYPE_DEFINITION     // Recursive: element type with constraints

// === OBJECT (0x22) ===
[if OBJECT]:
  varint field_count
  // For each field:
    ident field_name       // ASCII only (a-z, A-Z, 0-9, _)
    DATA_TYPE_DEFINITION   // Recursive: field type with constraints
```

### 4.4 Schema Definition Examples

**Simple property with constraints:**
```
Property: brightness (id=1)
  type_id: UINT8
  validation_flags: has_min | has_max | has_step
  min: 0
  max: 255
  step: 1
  ui_hint: slider
```

**Array property:**
```
Property: rgb (id=2)
  type_id: ARRAY
  element_count: 3
  element_type:
    type_id: UINT8
    validation_flags: has_min | has_max
    min: 0
    max: 255
  ui_hint: color_picker
```

**List with constrained elements:**
```
Property: temperatures (id=3)
  type_id: LIST
  length_constraints: has_min_length | has_max_length
  min_length: 1
  max_length: 100
  element_type:
    type_id: INT32
    validation_flags: has_min | has_max
    min: -40
    max: 125
```

**Object with nested types:**
```
Property: device_config (id=4)
  type_id: OBJECT
  field_count: 2
  field[0]:
    name: "name"
    type_id: LIST
    element_type:
      type_id: UINT8
      validation_flags: has_min | has_max
      min: 32   // Printable ASCII
      max: 126
  field[1]:
    name: "position"
    type_id: OBJECT
    field_count: 2
    field[0]:
      name: "x"
      type_id: INT32
      validation_flags: has_min
      min: 0
    field[1]:
      name: "y"
      type_id: INT32
      validation_flags: has_min
      min: 0
```

### 4.5 SCHEMA_DELETE (opcode 0x4)

Removes a schema definition.

**Flags**:
```
bit0: batch           // 1 = batched (batch_count follows)
bit1-3: reserved
```

```
u8 operation_header { opcode: 0x4, flags: see above }
[u8 batch_count]      // If batch=1 (count-1, so 0 means 1 item)

// For each deletion:
u8 item_type_flags {
    type: bit4             // 0=namespace, 1=property, 2=function
    reserved: bit4
}
propid item_id             // 1-2 bytes (see section 3.3)
```

---

## 5. Property Operations

Property operations transmit actual data values. These are the most frequent messages.

### 5.1 PROPERTY_UPDATE (opcode 0x1)

Updates property values. Uses propid encoding for property IDs (1-2 bytes, supports 0-32767).

**Flags**:
```
bit0: batch            // 1 = batched (batch_count follows)
bit1: has_timestamp    // Include timestamp (once for entire batch)
bit2-3: reserved
```

```
u8 operation_header { opcode: 0x1, flags: see above }
[u8 batch_count]       // If batch=1 (count-1, so 0 means 1 item)
[varint timestamp]         // Unix timestamp (if has_timestamp=1, once for entire batch)

// For each property update:
propid property_id         // 1-2 bytes (see section 3.3)
[varint version]           // If schema.level != LOCAL (GROUP/GLOBAL properties)
[varint source_node_id]    // If schema.level != LOCAL (GROUP/GLOBAL properties)
bytes value                // Encoded according to property's data_type_id
```

**Note**: Receiver determines if version/source_node_id are present by looking up property's `level` in schema. LOCAL properties have no version fields; GROUP/GLOBAL properties always include them.

### 5.2 Property Update Examples

**Example 1: LOCAL property update (brightness)**
```
Property schema: id=1, type=UINT8, level=LOCAL, name="brightness"
Update message:
  0x01              // opcode=1, flags=0, batch=0
  0x01              // property_id=1
  0x80              // value=128
Total: 3 bytes
```

**Example 2: GLOBAL property update with versioning (WiFi credentials)**
```
Property schema: id=10, type=LIST(OBJECT{ssid,password}), level=GLOBAL, name="known_wifi"
Update message:
  0x01              // opcode=1, flags=0, batch=0
  0x0A              // property_id=10
  0x05              // version=5 (varint) - included because schema.level=GLOBAL
  0x80 0x20         // source_node_id=4096 (varint)
  [value bytes...]  // Encoded LIST
Total: 5 bytes + value
```

**Example 3: Batched brightness + RGB update (LOCAL)**
```
Properties: id=1 (brightness, UINT8, LOCAL), id=2 (rgb, ARRAY[3](UINT8), LOCAL)
Update message:
  0x11              // opcode=1, flags=0, batch=1
  0x01              // batch_count=1 (means 2 operations)
  0x01 0xFF         // prop_id=1, value=255
  0x02 0xFF 0x00 0x00  // prop_id=2, RGB values
Total: 7 bytes
```

**Example 4: Animation name update (string property)**
```
Property schema: id=10, type=LIST(UINT8), name="current_animation"
Update message:
  0x01              // opcode=1, flags=0, batch=0
  0x0A              // property_id=10
  0x07              // string length (varint)
  "rainbow"         // UTF-8 string bytes
Total: 10 bytes
```

---

## 6. RPC Operations

Remote Procedure Call operations allow bidirectional function invocation. Either side can call functions defined in the other's schema. Uses flag bit0 to distinguish request (0) from response (1).

**Use cases**:
- Client→Server: User actions (setAnimation, reset, etc.)
- Server→Client: UI actions (showNotification, requestConfirmation, etc.)

### 6.1 RPC Request (opcode 0x5, flag bit0 = 0)

Invoke a function on the remote side.

**Flags**:
```
bit0: is_response      // 0 = request
bit1: needs_response   // 1 = wait for response, 0 = fire-and-forget
bit2-3: reserved
```

```
u8 operation_header { opcode: 0x5, flags: see above }
propid function_id         // Function to call (1-2 bytes, see section 3.3)
[u8 call_id]               // Only if needs_response=1 (for matching response)
bytes params               // All parameters concatenated, encoded per function schema
```

**call_id rules** (only when needs_response=1):
- Range 0-255, wraps around after 255
- Caller must not reuse a call_id until response received or timeout
- Recommended: increment sequentially, track pending calls in a 256-entry bitmap
- **Timeout**: 60 seconds. If no response received, caller should consider the call failed and may reuse the call_id

### 6.2 RPC Response (opcode 0x5, flag bit0 = 1)

Respond to an RPC call.

**Flags**:
```
bit0: is_response        // 1 = response
bit1: success            // 1 = success, 0 = error
bit2: has_return_value   // 1 = value present, 0 = void (only if success=1)
bit3: reserved
```

```
u8 operation_header { opcode: 0x5, flags: see above }
u8 call_id                 // Matches request call_id
// If success=1 and has_return_value=1:
  bytes return_value       // Encoded according to function schema
// If success=0:
  u8 error_code
  utf8 error_message
```

### 6.3 RPC Examples

**Example 1: Client calls server function (fire-and-forget, no params)**
```
Function schema: id=1, name="reset", params=[], return=void
Request message:
  0x05              // opcode=5, flags=0b0000 (request, no response needed)
  0x01              // function_id=1 (propid, 1 byte)
                    // (no call_id for fire-and-forget)
                    // (no params)
Total: 2 bytes
```

**Example 2: Client calls server "setAnimation"**
```
Function schema: id=2, name="setAnimation", params=[LIST(UINT8)], return=BOOL
Request message:
  0x25              // opcode=5, flags=0b0010 (request, needs_response=1)
  0x02              // function_id=2 (propid, 1 byte)
  0x0A              // call_id=10
  0x07 "rainbow"    // params: LIST(UINT8) encoded per schema
Total: 11 bytes

Response message:
  0x75              // opcode=5, flags=0b0111 (response, success=1, has_return_value=1)
  0x0A              // call_id=10
  0x01              // BOOL value=true
Total: 3 bytes
```

**Example 3: Server calls client "showNotification"**
```
Function schema: id=5, name="showNotification", params=[LIST(UINT8), UINT8], return=void
Request message (Server→Client):
  0x05              // opcode=5, flags=0b0000 (request, no response needed)
  0x05              // function_id=5 (propid, 1 byte)
                    // (no call_id for fire-and-forget)
  0x0F "Update complete"  // params: LIST(UINT8) + UINT8, encoded per schema
  0x01              // UINT8 severity=1 (info)
Total: 20 bytes
```

**Example 4: RPC error response**
```
Response message:
  0x35              // opcode=5, flags=0b0011 (response, success=0)
  0x0B              // call_id=11
  0x04              // error_code=4 (NOT_FOUND)
  0x13              // message length varint=19
  "Animation not found"  // UTF-8 error message
Total: 24 bytes
```

---

## 7. Synchronization and Conflict Resolution

### 7.1 State Synchronization

State synchronization is handled through the HELLO opcode (see section 2.3).

When a node needs to synchronize state with another:
1. Send HELLO to request complete state
2. Receive HELLO acknowledgment
3. Receive SCHEMA_UPSERT (batched)
4. Receive PROPERTY_UPDATE (batched)

**Note**: For mesh networks, nodes may broadcast PROPERTY_UPDATE messages directly without explicit sync request. Each node maintains its own state and updates based on received messages.

### 7.2 Conflict Resolution

**LOCAL properties**: No conflicts (single owner)

**GROUP/GLOBAL properties**: Use version-based conflict resolution

**Version-based Last-Write-Wins**:
1. Each property has monotonic version counter
2. When node updates property: version++, broadcast with source_node_id
3. Receiving nodes compare versions:
   - If received version > local version: accept update
   - If received version < local version: ignore (stale)
   - If received version == local version: compare source_node_id (higher wins)

**Example conflict:**
```
Node A (id=0x1000): brightness v3 = 100
Node B (id=0x2000): brightness v3 = 200  (concurrent update)

Node A receives B's update:
  - Same version (3 == 3)
  - Compare node_id: 0x2000 > 0x1000
  - Accept B's value (200), increment to v4

Result: Both nodes converge to brightness=200, v4
```

**No clock synchronization needed** - version counters provide total ordering

---

## 8. Control Operations

### 8.1 ERROR (opcode 0x7)

Generic error message for protocol violations or unexpected conditions.

**Flags**:
```
bit0: schema_mismatch    // 1 = state inconsistent, client should resync via HELLO
bit1-3: reserved
```

**Client behavior**: When `schema_mismatch=1`, the client's local schema or state is inconsistent with the server. The client should send HELLO to resynchronize. When `schema_mismatch=0`, the error is operational (e.g., validation failed) and the client can continue normally.

```
u8 operation_header { opcode: 0x7, flags: 0x0 }
u16 error_code {
    // Standard error codes:
    0x0000: SUCCESS
    0x0001: INVALID_OPCODE
    0x0002: INVALID_PROPERTY_ID
    0x0003: INVALID_FUNCTION_ID
    0x0004: TYPE_MISMATCH
    0x0005: VALIDATION_FAILED
    0x0006: OUT_OF_RANGE
    0x0007: PERMISSION_DENIED
    0x0008: NOT_IMPLEMENTED
    0x0009: PROTOCOL_VERSION_MISMATCH
    0x000A: BUFFER_OVERFLOW
    0x000B-0xFFFF: Application-specific errors
}
utf8 message               // Error description
[u8 related_opcode]        // Optional: opcode that caused error
```

### 8.2 PING (opcode 0x6)

Heartbeat to verify connection liveness.

**Flags**:
```
bit0: is_response    // 0=request, 1=response
bit1-3: reserved
```

```
u8 operation_header { opcode: 0x6, flags: see above }
varint payload             // Echo payload, typically incrementing counter
```

**Usage**:
- Client sends PING request (flag=0) periodically
- Server responds with PING response (flag=1) echoing same payload
- If no response received within timeout, consider connection lost

**Example heartbeat sequence**:
```
Client → Server: PING request
  06 01                    // opcode=6, flags=0 (request), payload=1

Server → Client: PING response
  16 01                    // opcode=6, flags=1 (response), payload=1

// 1 second later...
Client → Server: PING request
  06 02                    // opcode=6, flags=0, payload=2

Server → Client: PING response
  16 02                    // opcode=6, flags=1, payload=2
```

**Recommended intervals**:
- PING interval: 1000ms
- Response timeout: 1000ms
- On timeout: Close connection, attempt reconnect

---

## 9. Resource Operations

Resources are managed through the RESOURCE property type (0x24). Resource **headers** are automatically synced via PROPERTY_UPDATE like any property. Resource **bodies** are fetched on-demand via explicit request/response operations.

All resource opcodes use flags: `is_response` (bit0) distinguishes request/response, `status` (bit1) indicates success/error for responses.

### 9.1 Resource Discovery

There is no RESOURCE_LIST opcode. Clients discover resources through the RESOURCE property type:

1. On connect, client receives SCHEMA_UPSERT with RESOURCE type definition (header_type + body_type)
2. Client receives PROPERTY_UPDATE with all resource headers (id, version, size, custom fields)
3. When resources change, server broadcasts updated headers via PROPERTY_UPDATE

This integrates resource discovery into the existing property sync mechanism.

### 9.2 RESOURCE_GET (opcode 0x8)

Fetch a resource body by property ID and resource ID. Returns entire body.

**Request** (flags: is_response=0):
```
u8 operation_header { opcode: 0x8, flags: 0 }
u8 request_id             // For matching response
propid property_id         // Which resource property (e.g., "shaders")
varint resource_id         // Which resource within that property
```

**Response OK** (flags: is_response=1, status=0):
```
u8 operation_header { opcode: 0x8, flags: 0b001 }
u8 request_id
blob data                  // Entire body (encoded per body_type)
```

**Response Error** (flags: is_response=1, status=1):
```
u8 operation_header { opcode: 0x8, flags: 0b011 }
u8 request_id
u8 error_code              // 1=not_found, 2=error
utf8 message
```

**Example**:
```
// Client already has headers via PROPERTY_UPDATE:
// shaders = [{id:1, version:3, size:2048, name:"rainbow"}, ...]

// Fetch "rainbow" body
Client: RESOURCE_GET property_id=5, resource_id=1
Server: Response OK, body = {code:"void main()...", speed:1.5, primary_color:[255,0,128]}
```

### 9.3 RESOURCE_PUT (opcode 0x9)

Create or update a resource. Can update header only, body only, or both. After successful completion, server broadcasts updated headers to all clients via PROPERTY_UPDATE.

**Flags**:
```
// Request (bit0=0):
bit0: is_response      // 0 = request
bit1: update_header    // 1 = header_value follows
bit2: update_body      // 1 = body_data follows
bit3: reserved

// Response (bit0=1):
bit0: is_response      // 1 = response
bit1: status           // 0 = ok, 1 = error
bit2-3: reserved
```

**Request**:
```
u8 operation_header { opcode: 0x9, flags: see above }
u8 request_id
propid property_id         // Which resource property
varint resource_id         // 0 = create new, else update existing
[if update_header]:
  bytes header_value       // Encoded per header_type (excludes implicit id/version/size)
[if update_body]:
  bytes body_data           // Encoded per body_type
```

**Response OK** (flags: 0b001):
```
u8 operation_header { opcode: 0x9, flags: 0b001 }
u8 request_id
varint resource_id         // Assigned or confirmed ID
```

**Response Error** (flags: 0b011):
```
u8 operation_header { opcode: 0x9, flags: 0b011 }
u8 request_id
u8 error_code              // 1=out_of_space, 2=invalid_data, 3=error
utf8 message
```

**Examples**:
```
// Create new shader with header and body (flags: 0b110)
Client: RESOURCE_PUT property_id=5, resource_id=0, flags=0b110,
        header={name:"pulse", author:"me", enabled:true},
        body={code:"void main()...", speed:1.5, primary_color:[255,0,128]}
Server: Response OK (flags: 0b001), resource_id=3

// Update only header (flags: 0b010)
Client: RESOURCE_PUT property_id=5, resource_id=3, flags=0b010,
        header={name:"pulse_v2", author:"me", enabled:false}
Server: Response OK, resource_id=3

// Update only body (flags: 0b100)
Client: RESOURCE_PUT property_id=5, resource_id=3, flags=0b100,
        body={code:"void main2()...", speed:2.0, primary_color:[0,255,0]}
Server: Response OK, resource_id=3

// Server broadcasts to ALL clients after each successful PUT:
// PROPERTY_UPDATE: shaders = [..., {id:3, version:2, size:1024, name:"pulse_v2", ...}]
```

### 9.4 RESOURCE_DELETE (opcode 0xA)

Delete a resource by property ID and resource ID. After successful deletion, server broadcasts updated headers to all clients via PROPERTY_UPDATE.

**Request** (flags: is_response=0):
```
u8 operation_header { opcode: 0xA, flags: 0 }
u8 request_id
propid property_id         // Which resource property
varint resource_id         // Resource to delete
```

**Response OK** (flags: is_response=1, status=0):
```
u8 operation_header { opcode: 0xA, flags: 0b001 }
u8 request_id
```

**Response Error** (flags: is_response=1, status=1):
```
u8 operation_header { opcode: 0xA, flags: 0b011 }
u8 request_id
u8 error_code              // 1=not_found, 2=error
utf8 message
```

**Delete example**:
```
Client: RESOURCE_DELETE property_id=5, resource_id=1
Server: Response OK

// Server broadcasts to ALL clients:
// PROPERTY_UPDATE: shaders = [{id:2, ...}, {id:3, ...}]  // id=1 removed
```

### 9.5 Resource vs Regular Property

| Aspect | Regular Property | Resource Property |
|--------|------------------|-------------------|
| Size | Small (< 256 bytes typical) | Large (KB to MB per resource) |
| Value sync | Automatic (full value) | Headers only (automatic), bodies on-demand |
| Transfer | Full value each time | Chunked streaming for bodies |
| Modification | Via PROPERTY_UPDATE | Via RESOURCE_PUT/DELETE only (read-only property) |
| Schema | Type + constraints | Header schema + body schema |
| Storage | NVS | Headers in NVS (per-resource), bodies in SPIFFS |
| Use case | Config, state, UI values | Shaders, scripts, configs, media |

### 9.6 Complete Resource Flow Example

```
1. Client connects
   ← SCHEMA_UPSERT: shaders (RESOURCE type)
       header_schema: [{name: LIST(UINT8)}, {author: LIST(UINT8)}, {enabled: BOOL}]
       body_schema: [{code: LIST(UINT8)}, {speed: FLOAT32}, {primary_color: ARRAY[3](UINT8)}]

   ← PROPERTY_UPDATE: shaders = [
       {id:1, version:3, size:2048, name:"rainbow", author:"me", enabled:true},
       {id:2, version:1, size:512, name:"fade", author:"you", enabled:true}
     ]

2. Client wants rainbow shader body
   → RESOURCE_GET property_id=5, resource_id=1
   ← Response OK: body = {code:"void main()...", speed:1.5, primary_color:[255,0,128]}

3. Another client uploads new shader
   → RESOURCE_PUT property=shaders, resource_id=0, headers={name:"pulse",...}, body={...}
   ← Response: resource_id=3, success

   Server broadcasts to ALL clients:
   ← PROPERTY_UPDATE: shaders = [
       {id:1, ...}, {id:2, ...},
       {id:3, version:1, size:1024, name:"pulse", author:"them", enabled:true}
     ]

4. Client deletes shader
   → RESOURCE_DELETE property=shaders, resource_id=1
   ← Response: success

   Server broadcasts:
   ← PROPERTY_UPDATE: shaders = [{id:2, ...}, {id:3, ...}]
```

---

## 10. Transport-Specific Considerations

### 10.1 WebSocket Transport

- **MTU**: Effectively unlimited (use up to 64KB packets)
- **Batching**: Batch aggressively (50-100 properties per message)
- **Compression**: Consider gzip for schema messages
- **Fragmentation**: Not needed, handled by TCP

### 10.2 BLE Transport

- **MTU**: Typically 23-512 bytes (negotiate during connection)
- **Batching**: Batch carefully to stay under MTU
- **Property filtering**: Only `ble_exposed` properties available via BLE
- **Schema**: Full HELLO → SCHEMA_UPSERT → PROPERTY_UPDATE flow (same as WebSocket, filtered to ble_exposed)
- **Characteristic**: Use single characteristic with notify + write

BLE GATT Service UUID: `4D50-0001-xxxx-xxxx-xxxxxxxxxxxx` (MP-*)
- Characteristic UUID: `4D50-0002-xxxx-xxxx-xxxxxxxxxxxx`
  - Properties: WRITE, NOTIFY
  - Max length: Negotiated MTU

**BLE connection flow:**
```
1. Phone discovers ESP32 via BLE advertising
2. Phone connects, sends HELLO
3. Server responds with HELLO
4. Server sends SCHEMA_UPSERT (only ble_exposed properties)
5. Server sends PROPERTY_UPDATE (only ble_exposed properties)
6. Bidirectional updates for ble_exposed properties
```

### 10.3 ESP-NOW / ESP-MESH Transport

- **MTU**: 250 bytes maximum
- **Batching**: Essential to stay under MTU limit
- **Broadcast**: Use for GROUP/GLOBAL property updates (one-to-many)
- **Reliability**: No ACK, may need application-level retry
- **Schema**: Pre-programmed on all nodes (same firmware), not transmitted
- **Addressing**: Use ESP32 MAC address as device_id (varint from HELLO)
- **Property levels**: Only GROUP/GLOBAL properties propagate via mesh

**Optimization for mesh**:
- Pre-assign property IDs (hardcoded schema in firmware)
- Use propid IDs <= 127 for single-byte encoding
- Always include version for GROUP/GLOBAL properties
- Batch multiple property updates per packet

**Example mesh packet (GLOBAL WiFi credentials update):**
```
0x11                    // PROPERTY_UPDATE, flags=0, batched
0x01                    // 2 properties
0x0A                    // property_id=10 (known_wifi, GLOBAL - version follows)
0x03                    // version=3 (varint)
0x80 0x20               // source_node_id=4096 (varint)
[value bytes...]        // LIST of OBJECT{ssid, password}
0x0B                    // property_id=11 (group_brightness, GROUP - version follows)
0x05                    // version=5 (varint)
0x80 0x20               // source_node_id=4096 (varint)
0xFF                    // brightness=255
Total: ~10 bytes + WiFi credentials (fits in 250 byte MTU)
```

**Mesh startup discovery:**
```
1. New node boots, broadcasts HELLO via ESP-NOW
2. Neighbors respond with HELLO (no schema, nodes have same firmware)
3. Neighbors broadcast current GROUP/GLOBAL property values with versions
4. New node adopts properties with latest versions
5. Node is now synced with mesh
```

---

## 11. Protocol Versioning

### 11.1 Version Compatibility

Protocol uses single-byte version number (current: 1).

**Version mismatch handling**:
- Client and server must have same protocol_version
- If versions differ, server sends ERROR with PROTOCOL_VERSION_MISMATCH
- Connection should be closed

**Future versions**:
- Version 2, 3, etc. may introduce breaking changes
- No backward compatibility guarantee across versions
- Keep protocol simple - avoid version negotiation complexity

---

## 12. Security Considerations

### 12.1 Authentication

Not built into protocol. Implement at transport layer:
- WebSocket: Use WS_SECURE (WSS) with token-based auth
- BLE: Use pairing and bonding
- ESP-MESH: Pre-shared keys for encryption

### 12.2 Validation

Always validate:
- Property IDs exist in schema
- Data types match schema definition
- Values pass validation rules (min/max/pattern)
- Array/string lengths within limits

Reject invalid messages with ERROR response.

### 12.3 Rate Limiting

Implement rate limiting to prevent:
- Property update flooding
- RPC call spam
- Schema request abuse

Recommended limits:
- 100 property updates/second per client
- 10 RPC calls/second per client
- 1 schema request per connection

---

## 13. Implementation Guidelines

### 13.1 Memory Management

**Embedded devices (ESP32)**:
- Pre-allocate property storage (fixed schema)
- Limit schema to 50-100 properties
- Reuse buffers for encoding/decoding
- Use ring buffer for queued updates

**Clients (browser/app)**:
- Dynamic schema storage (Map/Dictionary)
- Generate UI from schema metadata
- Debounce rapid property updates for UI rendering

### 13.2 Error Handling

**On decode error**:
1. Send ERROR message with specific error code
2. Log error for debugging
3. Continue processing (don't disconnect)

**On validation failure**:
1. Send ERROR with VALIDATION_FAILED code
2. Include property name in error message
3. Don't update property value

**On unknown opcode**:
1. Send ERROR with INVALID_OPCODE
2. Check protocol version mismatch
3. Ignore message and continue

### 13.3 Testing

**Test cases to implement**:
- Round-trip encoding/decoding for all types
- Batching with mixed property types
- Large string/array handling
- Varint edge cases (0, 127, 128, 65535)
- MTU boundary conditions for each transport
- Concurrent property updates (race conditions)
- Schema updates during active connection
- RPC timeout handling

---

## 14. Example Property Definitions

### 14.1 LED Controller Properties

```
Namespace: "system" (id=0)
  - device_name: LIST(UINT8), readonly, LOCAL
  - uptime_ms: INT32, readonly, LOCAL
  - free_memory: INT32, readonly, LOCAL

Namespace: "wifi" (id=1)
  - current_ssid: LIST(UINT8), persistent, LOCAL
  - current_password: LIST(UINT8), persistent, hidden, LOCAL
  - connected: BOOL, readonly, LOCAL
  - ip_address: LIST(UINT8), readonly, LOCAL
  - known_wifi_credentials: LIST(OBJECT{
      ssid: LIST(UINT8),
      password: LIST(UINT8)
    }), persistent, GLOBAL, ble_exposed

Namespace: "led" (id=2)
  - brightness: UINT8, min=0, max=255, step=1, unit="%", widget=slider, LOCAL
  - group_brightness: UINT8, min=0, max=255, step=1, unit="%", widget=slider, GROUP, ble_exposed
  - active_leds: INT32, min=1, max=200, persistent, LOCAL
  - current_animation: LIST(UINT8), readonly, LOCAL
  - rgb: ARRAY[3](UINT8), widget=color_picker, LOCAL

Namespace: "animation" (id=3)
  - speed: FLOAT32, min=0.1, max=10.0, step=0.1, unit="x", GROUP
  - color_primary: ARRAY[3](UINT8), widget=color_picker, GROUP
  - color_secondary: ARRAY[3](UINT8), widget=color_picker, GROUP
  - available_animations: LIST(LIST(UINT8)), readonly, GLOBAL

Functions:
  - nextAnimation(): void
  - previousAnimation(): void
  - setAnimation(name: LIST(UINT8)): BOOL
  - reset(): void
```

---

## Appendix A: Wire Format Quick Reference

| Type | Encoding |
|------|----------|
| BOOL | u8 (0 or 1) |
| INT8 | i8 |
| UINT8 | u8 |
| INT32 | i32 little-endian |
| FLOAT32 | IEEE 754 single, little-endian |
| ARRAY[n] | n * element_size (fixed, no overhead) |
| LIST | varint count + elements |
| OBJECT | field values in schema order (fixed, no overhead) |

## Appendix B: Complete Message Examples

**Full connection sequence**:
```
Client → Server: HELLO
  00 01 00 00 80 01 00 ...

Server → Client: HELLO
  00 01 00 00 80 01 ...

Server → Client: SCHEMA_UPSERT (batched, 5 properties)
  33 04 11 01 00 0A "brightness" ...

Server → Client: PROPERTY_UPDATE (batched, initial values)
  11 02 01 00 80 02 00 FF 00 00 ...

Client → Server: PROPERTY_UPDATE (brightness=200)
  01 01 C8

Server → Client: PROPERTY_UPDATE (brightness=200, echo)
  01 01 C8

// Later, client needs resync:
Client → Server: HELLO (resync request)
  00 01 00 00 80 01 00 ...

Server → Client: HELLO + SCHEMA_UPSERT + PROPERTY_UPDATE (complete state)
```

---

## Changelog

### v1 (2025-10-06)
- Initial protocol specification
- 8 core opcodes (HELLO, PROPERTY_UPDATE x2, SCHEMA_UPSERT/DELETE, RPC_CALL/RESPONSE, ERROR)
- Type system with 13 basic types + composites
- Bidirectional RPC support
- HELLO-based synchronization
- Transport-specific optimizations (WebSocket, BLE, ESP-NOW)
- Single-byte protocol version
- Variable-length device IDs and timestamps (varint)
- Removed feature negotiation (batching always supported)

---

**End of MicroProto Protocol Specification v1**