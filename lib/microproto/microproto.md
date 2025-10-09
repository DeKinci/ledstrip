# MicroProto Protocol Specification v1.0

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
    flags: bit3,         // Operation-specific flags
    batch_flag: bit1     // 1 if batched, 0 if single
}
```

If `batch_flag = 1`, a batch count follows:
```
u8 batch_count       // Number of operations in batch (1-255, value is count-1, so 0 = 1 operation)
```

### 1.2 Operation Codes (opcodes)

| Opcode | Name | Direction | Description |
|--------|------|-----------|-------------|
| 0x0 | HELLO | Bidirectional | Protocol handshake and version negotiation |
| 0x1 | PROPERTY_UPDATE_SHORT | Bidirectional | Property update with u8 ID |
| 0x2 | PROPERTY_UPDATE_LONG | Bidirectional | Property update with u16 ID |
| 0x3 | SCHEMA_UPSERT | Server→Client | Create or update schema definition |
| 0x4 | SCHEMA_DELETE | Server→Client | Delete schema definition |
| 0x5 | RPC_CALL | Bidirectional | Call remote function |
| 0x6 | RPC_RESPONSE | Bidirectional | Response to RPC call |
| 0x7 | ERROR | Bidirectional | Error message |
| 0x8-0xF | RESERVED | - | Reserved for future use |

---

## 2. Connection Lifecycle

### 2.1 HELLO Operation - Connection and Synchronization

HELLO serves dual purposes:
- **Initial connection**: Negotiate protocol version and capabilities
- **Resynchronization**: Client can send HELLO at any time to request full state reset

### 2.2 Connection Establishment

When a client connects or needs to resync:

1. **Client sends HELLO**:
```
u8 operation_header { opcode: 0x0, flags: 0, batch: 0 }
u8 protocol_version        // Protocol version (current: 1)
u16 max_packet_size        // Maximum packet size client can receive (little-endian)
u32 device_id              // Unique device identifier (little-endian)
```

**Semantics**: "I am (re)connecting. Please send me complete state."

2. **Server responds with HELLO**:
```
u8 operation_header { opcode: 0x0, flags: 0, batch: 0 }
u8 protocol_version        // Protocol version (current: 1)
u16 max_packet_size        // Maximum packet size server can receive (little-endian)
u32 session_id             // Unique session identifier (little-endian)
u32 server_timestamp       // Unix timestamp for sync (little-endian)
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

If client becomes out-of-sync (e.g., missed messages, timeout, network hiccup):
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
| 0x20 | ARRAY | Fixed-size homogeneous sequence. Schema defines count and element property definition. Only values transmitted (no count/type overhead) |
| 0x21 | LIST | Variable-size homogeneous sequence. Schema defines element property definition. Count sent with data |
| 0x22 | OBJECT | Fixed-size heterogeneous structure. Schema defines field names and field property definitions. Only values transmitted in schema-defined order |
| 0x23-0x2F | RESERVED | Reserved for future composite types |

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

### 3.3 Variable-Length Integer Encoding (varint)

Variable-length integers use continuation bit encoding (similar to Protobuf):
- Bits 0-6: Data bits
- Bit 7: Continuation bit (1 = more bytes follow, 0 = last byte)

Examples:
- `0x00` → 0
- `0x7F` → 127
- `0x80 0x01` → 128
- `0xFF 0xFF 0x03` → 65535

Max 5 bytes for 32-bit values, 10 bytes for 64-bit values.

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
  u8 field_name_length   // Max 255 chars
  bytes field_name       // ASCII only (a-z, A-Z, 0-9, _)
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

```
u8 operation_header { opcode: 0x3, flags: see below, batch: 0/1 }
[u8 batch_count]           // If batched

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
    large_id: bit1         // 0=u8 id, 1=u16 id
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

u8/u16 item_id             // Unique identifier
u8/u16 namespace_id        // Parent namespace (0 = root)
u8 name_length             // ASCII name length
bytes name                 // Property/function name (ASCII: a-z, A-Z, 0-9, _)
varint description_length
bytes description          // Human-readable description (UTF-8)

// For PROPERTY type only:
DATA_TYPE_DEFINITION       // Recursive type definition (see 4.3)
bytes default_value        // Encoded according to data type

// UI hints (optional, for auto-generated UIs):
u8 ui_hints_flags {
    has_widget_hint: bit1
    has_unit: bit1
    has_icon: bit1
    has_color: bit1
    reserved: bit4
}
[u8 widget_hint]           // 0=auto, 1=slider, 2=toggle, 3=color_picker, 4=text_input, etc.
[varint unit_length]       // Unit string (e.g., "ms", "%", "°C")
  [bytes unit]             // ASCII
[varint icon_length]       // Icon identifier
  [bytes icon]             // ASCII
[u32 color]                // RGBA color for UI (little-endian)

// For FUNCTION type only:
u8 param_count             // Number of parameters
// For each parameter:
  [u8 param_name_length]
  [bytes param_name]       // ASCII
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
    reserved: bit5
  }
  [varint min_length]      // Minimum list length
  [varint max_length]      // Maximum list length
  DATA_TYPE_DEFINITION     // Recursive: element type with constraints

// === OBJECT (0x22) ===
[if OBJECT]:
  varint field_count
  // For each field:
    u8 field_name_length
    bytes field_name       // ASCII only
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

```
u8 operation_header { opcode: 0x4, flags: 0, batch: 0/1 }
[u8 batch_count]

// For each deletion:
u8 item_type_flags {
    type: bit4             // 0=namespace, 1=property, 2=function
    large_id: bit1
    reserved: bit3
}
u8/u16 item_id
```

---

## 5. Property Operations

Property operations transmit actual data values. These are the most frequent messages.

### 5.1 PROPERTY_UPDATE_SHORT (opcode 0x1)

Updates properties with u8 IDs (supports 0-255 properties).

```
u8 operation_header { opcode: 0x1, flags: see below, batch: 0/1 }
[u8 batch_count]

// For each property update:
u8 property_id
u8 update_flags {
    has_timestamp: bit1    // Include update timestamp
    force_notify: bit1     // Force notify even if value unchanged
    has_version: bit1      // Include version (for GROUP/GLOBAL properties)
    reserved: bit5
}
[u32 timestamp]            // Unix timestamp (if has_timestamp=1)
[u32 version]              // Monotonic version counter (if has_version=1, for GROUP/GLOBAL)
[u32 source_node_id]       // Node that made this update (if has_version=1)
bytes value                // Encoded according to property's data_type_id
```

### 5.2 PROPERTY_UPDATE_LONG (opcode 0x2)

Updates properties with u16 IDs (supports 0-65535 properties).

```
u8 operation_header { opcode: 0x2, flags: see below, batch: 0/1 }
[u8 batch_count]

// For each property update:
u16 property_id            // Little-endian
u8 update_flags {
    has_timestamp: bit1
    force_notify: bit1
    has_version: bit1      // Include version (for GROUP/GLOBAL properties)
    reserved: bit5
}
[u32 timestamp]
[u32 version]              // Monotonic version counter (if has_version=1, for GROUP/GLOBAL)
[u32 source_node_id]       // Node that made this update (if has_version=1)
bytes value                // Encoded according to property's data_type_id
```

### 5.3 Property Update Examples

**Example 1: LOCAL property update (brightness)**
```
Property schema: id=1, type=UINT8, level=LOCAL, name="brightness"
Update message:
  0x01              // opcode=1 (short), batch=0
  0x01              // property_id=1
  0x00              // no flags (LOCAL properties don't need version)
  0x80              // value=128
Total: 4 bytes
```

**Example 2: GLOBAL property update with versioning (WiFi credentials)**
```
Property schema: id=10, type=LIST(OBJECT{ssid,password}), level=GLOBAL, name="known_wifi"
Update message:
  0x01              // opcode=1 (short), batch=0
  0x0A              // property_id=10
  0x04              // has_version=1
  0x00 0x00 0x00 0x05  // version=5
  0x12 0x34 0x56 0x78  // source_node_id=0x78563412
  [value bytes...]     // Encoded LIST
Total: 11 bytes + value
```

**Example 3: Batched brightness + RGB update**
```
Properties: id=1 (brightness, UINT8, LOCAL), id=2 (rgb, ARRAY[3](UINT8), LOCAL)
Update message:
  0x11              // opcode=1, batch=1
  0x01              // batch_count=1 (means 2 operations)
  0x01 0x00 0xFF    // prop_id=1, flags=0, value=255
  0x02 0x00 0xFF 0x00 0x00  // prop_id=2, RGB values only (no overhead)
Total: 9 bytes
```

**Example 4: Animation name update (string property)**
```
Property schema: id=10, type=LIST(UINT8), name="current_animation"
Update message:
  0x01              // opcode=1, batch=0
  0x0A              // property_id=10
  0x00              // no flags
  0x07              // string length (varint)
  "rainbow"         // UTF-8 string bytes
Total: 10 bytes
```

---

## 6. RPC Operations

Remote Procedure Call operations allow bidirectional function invocation. Either side can call functions defined in the other's schema.

**Use cases**:
- Client→Server: User actions (setAnimation, reset, etc.)
- Server→Client: UI actions (showNotification, requestConfirmation, etc.)

### 6.1 RPC_CALL (opcode 0x5)

Invoke a function on the remote side.

```
u8 operation_header { opcode: 0x5, flags: see below, batch: 0 }
u8 call_flags {
    needs_response: bit1   // 1=wait for response, 0=fire-and-forget
    large_id: bit1         // 0=u8 function_id, 1=u16 function_id
    reserved: bit6
}
u8/u16 function_id         // Function to call
u16 call_id                // Unique call identifier (for matching response)
u8 param_count             // Number of parameters
// For each parameter:
  bytes param_value        // Encoded according to function schema
```

### 6.2 RPC_RESPONSE (opcode 0x6)

Respond to an RPC call.

```
u8 operation_header { opcode: 0x6, flags: see below, batch: 0 }
u16 call_id                // Matches RPC_CALL call_id
u8 status {
    success: bit1          // 1=success, 0=error
    has_return_value: bit1 // 1=value present, 0=void
    reserved: bit6
}
// If success=1 and has_return_value=1:
  bytes return_value       // Encoded according to function schema
// If success=0:
  varint error_code
  varint error_message_length
  bytes error_message      // ARRAY(UINT8) - UTF-8 error description
```

### 6.3 RPC Examples

**Example 1: Client calls server function (no response)**
```
Function schema: id=1, name="reset", params=[], return=void
Call message:
  0x50              // opcode=5, batch=0
  0x00              // needs_response=0, large_id=0
  0x01              // function_id=1
  0x00 0x01         // call_id=1 (ignored for fire-and-forget)
  0x00              // param_count=0
Total: 5 bytes
```

**Example 2: Client calls server "setAnimation"**
```
Function schema: id=2, name="setAnimation", params=[LIST(UINT8)], return=BOOL
Call message:
  0x50              // opcode=5, batch=0
  0x01              // needs_response=1, large_id=0
  0x02              // function_id=2
  0x00 0x0A         // call_id=10
  0x01              // param_count=1
  0x07 "rainbow"    // LIST(UINT8) string param
Total: 13 bytes

Response message:
  0x60              // opcode=6, batch=0
  0x00 0x0A         // call_id=10
  0x03              // success=1, has_return_value=1
  0x01              // BOOL value=true
Total: 5 bytes
```

**Example 3: Server calls client "showNotification"**
```
Function schema: id=5, name="showNotification", params=[LIST(UINT8), UINT8], return=void
Call message (Server→Client):
  0x50              // opcode=5, batch=0
  0x00              // needs_response=0, large_id=0
  0x05              // function_id=5
  0x00 0x14         // call_id=20
  0x02              // param_count=2
  0x0F "Update complete"  // LIST(UINT8) string param
  0x01              // UINT8 severity=1 (info)
Total: 23 bytes
```

**Example 4: RPC error response**
```
Response message:
  0x60              // opcode=6, batch=0
  0x00 0x0B         // call_id=11
  0x00              // success=0
  0x04              // error_code=4 (NOT_FOUND)
  0x13              // message length varint=19
  0x13 "Animation not found"  // LIST(UINT8) error message
Total: 26 bytes
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

```
u8 operation_header { opcode: 0x7, flags: see below, batch: 0 }
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
varint message_length
bytes message              // ARRAY(UINT8) - UTF-8 error description
[u8 related_opcode]        // Optional: opcode that caused error
```

---

## 9. Transport-Specific Considerations

### 9.1 WebSocket Transport

- **MTU**: Effectively unlimited (use up to 64KB packets)
- **Batching**: Batch aggressively (50-100 properties per message)
- **Compression**: Consider gzip for schema messages
- **Fragmentation**: Not needed, handled by TCP

### 9.2 BLE Transport

- **MTU**: Typically 23-512 bytes (negotiate during connection)
- **Batching**: Batch carefully to stay under MTU
- **Property filtering**: Only `ble_exposed` properties available via BLE
- **Schema**: Not sent via BLE (phone app has hardcoded schema or fetches via WiFi)
- **Property updates**: Only expose essential properties (WiFi creds, brightness, animation)
- **Characteristic**: Use single characteristic with notify + write

BLE GATT Service UUID: `4D50-0001-xxxx-xxxx-xxxxxxxxxxxx` (MP-*)
- Characteristic UUID: `4D50-0002-xxxx-xxxx-xxxxxxxxxxxx`
  - Properties: WRITE, NOTIFY
  - Max length: Negotiated MTU

**BLE provisioning flow:**
```
1. Phone discovers ESP32 via BLE advertising
2. Phone connects (no HELLO/schema exchange)
3. Phone sends PROPERTY_UPDATE for GLOBAL known_wifi_credentials
4. ESP32 validates (property must have ble_exposed flag)
5. ESP32 broadcasts to mesh
6. All nodes receive WiFi credentials
```

### 9.3 ESP-NOW / ESP-MESH Transport

- **MTU**: 250 bytes maximum
- **Batching**: Essential to stay under MTU limit
- **Broadcast**: Use for GROUP/GLOBAL property updates (one-to-many)
- **Reliability**: No ACK, may need application-level retry
- **Schema**: Pre-programmed on all nodes (same firmware), not transmitted
- **Addressing**: Use ESP32 MAC address as device_id (u32 from HELLO)
- **Property levels**: Only GROUP/GLOBAL properties propagate via mesh

**Optimization for mesh**:
- Pre-assign property IDs (hardcoded schema in firmware)
- Use PROPERTY_UPDATE_SHORT exclusively (u8 IDs only)
- Always include version for GROUP/GLOBAL properties
- Batch multiple property updates per packet

**Example mesh packet (GLOBAL WiFi credentials update):**
```
0x11                    // PROPERTY_UPDATE_SHORT, batched
0x01                    // 2 properties
0x0A 0x04               // property_id=10 (known_wifi), has_version=1
0x00 0x00 0x00 0x03     // version=3
0x12 0x34 0x56 0x78     // source_node_id
[value bytes...]        // LIST of OBJECT{ssid, password}
0x0B 0x04               // property_id=11 (group_brightness), has_version=1
0x00 0x00 0x00 0x05     // version=5
0x12 0x34 0x56 0x78     // source_node_id
0xFF                    // brightness=255
Total: ~20 bytes + WiFi credentials (fits in 250 byte MTU)
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

## 10. Protocol Versioning

### 10.1 Version Compatibility

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

## 11. Security Considerations

### 11.1 Authentication

Not built into protocol. Implement at transport layer:
- WebSocket: Use WS_SECURE (WSS) with token-based auth
- BLE: Use pairing and bonding
- ESP-MESH: Pre-shared keys for encryption

### 11.2 Validation

Always validate:
- Property IDs exist in schema
- Data types match schema definition
- Values pass validation rules (min/max/pattern)
- Array/string lengths within limits

Reject invalid messages with ERROR response.

### 11.3 Rate Limiting

Implement rate limiting to prevent:
- Property update flooding
- RPC call spam
- Schema request abuse

Recommended limits:
- 100 property updates/second per client
- 10 RPC calls/second per client
- 1 schema request per connection

---

## 12. Implementation Guidelines

### 12.1 Memory Management

**Embedded devices (ESP32)**:
- Pre-allocate property storage (fixed schema)
- Limit schema to 50-100 properties
- Reuse buffers for encoding/decoding
- Use ring buffer for queued updates

**Clients (browser/app)**:
- Dynamic schema storage (Map/Dictionary)
- Generate UI from schema metadata
- Debounce rapid property updates for UI rendering

### 12.2 Error Handling

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

### 12.3 Testing

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

## 13. Example Property Definitions

### 13.1 LED Controller Properties

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
  01 01 00 C8

Server → Client: PROPERTY_UPDATE (brightness=200, echo)
  01 01 00 C8

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
- Fixed 4-byte device IDs
- Removed feature negotiation (batching always supported)

---

**End of MicroProto Protocol Specification v1**