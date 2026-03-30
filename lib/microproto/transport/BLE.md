# MicroProto BLE Transport

BLE GATT server on ESP32 allowing phones and other devices to control LED properties via MicroProto binary protocol over Bluetooth Low Energy.

## GATT Service Layout

| Item | UUID | Properties |
|------|------|------------|
| **Service** | `e3a10001-f5a3-4aa0-b726-5d1be14a1d00` | - |
| **TX Characteristic** | `e3a10003-f5a3-4aa0-b726-5d1be14a1d00` | Notify |
| **RX Characteristic** | `e3a10002-f5a3-4aa0-b726-5d1be14a1d00` | Write, Write Without Response |

- **TX** = ESP32 → Phone (server sends notifications here)
- **RX** = Phone → ESP32 (client writes MicroProto messages here)
- Advertised device name: `SmartGarland`
- Max simultaneous BLE connections: 3

## Server Implementation

Source: `MicroProtoBleServer.h` / `MicroProtoBleServer.cpp`

### Architecture

- Implements `MessageHandler` (same as the WebSocket `MicroProtoServer`)
- Uses `MessageRouter` for opcode dispatch
- BLE callbacks (`onWrite`) run on the NimBLE task; incoming messages are queued to a ring buffer and processed on the main task in `loop()`
- Property change broadcasts use `PropertySystem::onFlush()` callback, rate-limited to ~15 Hz
- Coexists with `BleDeviceManager` (BLE client role for HID remotes) - NimBLE handles radio time-sharing

### MTU Handling & Fragmentation

- Default BLE MTU is 23 bytes (20 byte ATT payload after 3-byte overhead)
- The server tracks per-client MTU via `onMTUChange` callback
- Messages larger than MTU are automatically **fragmented** using a 1-byte fragment header per BLE notification/write:

```
Bit 7 (0x80): START — first (or only) fragment
Bit 6 (0x40): END   — last (or only) fragment

0xC0 = complete message (no fragmentation)
0x80 = first fragment
0x00 = middle fragment
0x40 = last fragment
```

- BLE guarantees in-order delivery per connection, so no sequence numbers are needed
- Overhead: 1 byte per BLE notification/write
- TX: `sendToClient()` splits large messages into (MTU-1)-sized chunks with fragment headers
- RX: per-client `BleReassembler` accumulates fragments and delivers complete messages to the processing queue
- Implementation: `BleFragmentation.h` (standalone, testable without NimBLE)

### Thread Safety

BLE characteristic write callbacks execute on the NimBLE task (not the Arduino main loop). The server uses a single-producer single-consumer ring buffer (`_rxQueue`) to safely transfer incoming messages to the main task:

1. `onWrite()` (NimBLE task): copies message data into next ring buffer slot
2. `processRxQueue()` (main task, called from `loop()`): processes queued messages through the `MessageRouter`

### Property Filtering

Only properties with `ble_exposed = true` are visible over BLE:
- **Schema sync**: only `ble_exposed` properties are sent in SCHEMA_UPSERT
- **Value sync**: only `ble_exposed` property values are sent in PROPERTY_UPDATE
- **Broadcasts**: property change notifications only sent for `ble_exposed` properties
- **Incoming writes**: updates to non-`ble_exposed` properties are rejected

### Current Scope

HELLO handshake + schema caching + property sync + property updates + ping/pong + RPC + resource operations (GET/PUT/DELETE).

---

## BLE Client Implementation Guide

### Connection Flow (Required Order)

```
1. Scan         → Find service UUID e3a10001-f5a3-4aa0-b726-5d1be14a1d00
2. Connect      → Establish BLE connection
3. Request MTU  → Request 512 (or at least 256) for larger messages
4. Discover     → Find TX and RX characteristics by UUID
5. Subscribe    → Enable notifications on TX characteristic (write 0x01 0x00 to CCCD)
6. Send HELLO   → Write HELLO request to RX characteristic
7. Receive sync → TX notifications deliver: HELLO response, schema, property values
8. Ready        → Bidirectional property updates
```

**Step 5 MUST happen before step 6.** If the client sends HELLO before subscribing to TX notifications, the server's response will be sent but the client's BLE stack won't deliver it.

### Fragment Header (Required)

**All BLE reads and writes carry a 1-byte fragment header.** This applies to both directions:
- **Client → Server** (writes to RX characteristic): prepend `0xC0` for complete messages
- **Server → Client** (TX notifications): parse the fragment header to reassemble

```
Header byte:
  0xC0 = complete message (START|END) — most client writes use this
  0x80 = first fragment of multi-part message
  0x00 = middle fragment
  0x40 = last fragment
```

For typical client writes (HELLO, property updates, pings), the message fits in one write — just prepend `0xC0`. For large writes that exceed MTU, split into fragments with START/middle/END headers.

**Example — writing a property update:**
```
C0 01 03 C8
│  │  │  └── value = 200 (uint8)
│  │  └───── property_id = 3
│  └──────── PROPERTY_UPDATE opcode
└─────────── fragment header: complete message
```

### Android (Kotlin) Example

```kotlin
// UUIDs
val SERVICE_UUID     = UUID.fromString("e3a10001-f5a3-4aa0-b726-5d1be14a1d00")
val RX_CHAR_UUID     = UUID.fromString("e3a10002-f5a3-4aa0-b726-5d1be14a1d00")
val TX_CHAR_UUID     = UUID.fromString("e3a10003-f5a3-4aa0-b726-5d1be14a1d00")
val CCCD_UUID        = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

// After connecting:
gatt.requestMtu(512)

// In onMtuChanged:
val service = gatt.getService(SERVICE_UUID)
val txChar = service.getCharacteristic(TX_CHAR_UUID)
val rxChar = service.getCharacteristic(RX_CHAR_UUID)

// Enable notifications on TX
gatt.setCharacteristicNotification(txChar, true)
val cccd = txChar.getDescriptor(CCCD_UUID)
cccd.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
gatt.writeDescriptor(cccd)

// In onDescriptorWrite (after CCCD write completes):
// NOW send HELLO — prepend 0xC0 fragment header
val hello = byteArrayOf(0xC0.toByte()) + buildHelloRequest()
rxChar.value = hello
rxChar.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
gatt.writeCharacteristic(rxChar)

// In onCharacteristicChanged (TX notifications):
// Each notification carries a 1-byte fragment header + payload.
// Reassemble fragments before parsing protocol messages.
fun onCharacteristicChanged(gatt: BluetoothGatt, char: BluetoothGattCharacteristic) {
    val data = char.value
    reassembler.feed(data)  // Accumulate fragments
    if (reassembler.isComplete()) {
        parseMessage(reassembler.message)  // Parse complete MicroProto message
        reassembler.reset()
    }
}
```

### iOS (Swift/CoreBluetooth) Example

```swift
let serviceUUID = CBUUID(string: "e3a10001-f5a3-4aa0-b726-5d1be14a1d00")
let rxCharUUID  = CBUUID(string: "e3a10002-f5a3-4aa0-b726-5d1be14a1d00")
let txCharUUID  = CBUUID(string: "e3a10003-f5a3-4aa0-b726-5d1be14a1d00")

// After discovering characteristics:
// Subscribe to TX notifications
peripheral.setNotifyValue(true, for: txCharacteristic)

// After subscription confirmed in didUpdateNotificationStateFor:
// Send HELLO — prepend 0xC0 fragment header
let hello = Data([0xC0]) + buildHelloRequest()
peripheral.writeValue(hello, for: rxCharacteristic, type: .withResponse)

// Receive in didUpdateValueFor:
// Each notification carries a 1-byte fragment header + payload.
// Reassemble fragments before parsing protocol messages.
func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor char: CBCharacteristic, error: Error?) {
    guard let data = char.value else { return }
    reassembler.feed(data)
    if reassembler.isComplete {
        parseMessage(reassembler.message)
        reassembler.reset()
    }
}
```

---

## MicroProto Binary Protocol Quick Reference

All messages start with a 1-byte operation header: `[opcode:4bits][flags:4bits]`.

All multi-byte integers are **little-endian**.

### Building a HELLO Request

All writes must be wrapped with a fragment header (see above). For a HELLO that fits in one write:

```
Byte 0:  0xC0          // fragment header: complete message
Byte 1:  0x00          // opcode=0 (HELLO), flags=0 (request)
Byte 2:  0x01          // protocol_version = 1
Byte 3+: varint        // max_packet_size (use negotiated MTU - 3, e.g., 509 for MTU 512)
Byte N+: varint        // device_id (any unique uint32, e.g., hash of MAC address)
u16:     schema_version // Cached schema version (0 = no cache, triggers full schema sync)
```

**Example** (MTU=256, device_id=42, no cached schema):
```
C0 00 01 FD 01 2A 00 00
│  │  │  │     │  └──── schema_version = 0 (u16 LE, no cache)
│  │  │  │     └────── device_id = 42
│  │  │  └──────────── max_packet_size = 253 (varint: 0xFD 0x01)
│  │  └─────────────── protocol_version = 1
│  └────────────────── opcode=HELLO, flags=0 (request)
└───────────────────── fragment header: complete message
```

### Parsing the HELLO Response

```
Byte 0:  0x10          // opcode=0 (HELLO), flags=1 (is_response)
Byte 1:  0x01          // protocol_version (must be 1)
Byte 2+: varint        // max_packet_size (server's max send size)
Byte N+: varint        // session_id
Byte M+: varint        // server_timestamp (seconds since boot)
u16:     schema_version // Current server schema version
```

If the client sent a non-zero `schema_version` that matches the server's, the server skips SCHEMA_UPSERT — the client uses its cached schema. PROPERTY_UPDATE is always sent.

### Parsing SCHEMA_UPSERT

Schema arrives as a batched message containing all BLE-exposed properties:

```
Byte 0:  0x13          // opcode=3 (SCHEMA_UPSERT), flags=1 (batched)
Byte 1:  count-1       // Number of properties minus 1
// For each property:
Byte 1:  item_type     // bits[3:0]=type (1=PROPERTY), bits[6:4]=flags (readonly, persistent, hidden)
Byte 2:  level_flags   // bits[1:0]=level, bit2=ble_exposed
[Byte 3:  group_id]    // Only if level=GROUP (1)
propid:  property_id   // 1-2 bytes
propid:  namespace_id  // 1-2 bytes (usually 0x00 = root)
ident:   name          // u8 length + ASCII bytes
utf8:    description   // varint length + UTF-8 bytes
         type_def      // DATA_TYPE_DEFINITION (recursive)
         default_value // Encoded per type
u8:      ui_hints      // bits[3:0]=has_widget|has_unit|has_icon, bits[7:4]=colorgroup
[u8:     widget]       // If has_widget
[u8+bytes: unit]       // If has_unit: u8 len + ASCII
[u8+bytes: icon]       // If has_icon: u8 len + UTF-8 emoji
```

### Parsing PROPERTY_UPDATE

Property values arrive as batched updates:

```
Byte 0:  0x11          // opcode=1 (PROPERTY_UPDATE), flags=1 (batch)
Byte 1:  count-1       // Number of properties minus 1 (0 = 1 property)
// For each property:
  propid: property_id  // 1-2 bytes
  bytes:  value        // Encoded per property's type (from schema)
```

### Sending a PROPERTY_UPDATE

To update a property value, write to the RX characteristic (with fragment header):

```
Byte 0:  0xC0          // fragment header: complete message
Byte 1:  0x01          // opcode=1 (PROPERTY_UPDATE), flags=0 (single, no timestamp)
propid:  property_id   // 1-2 bytes (from schema)
bytes:   value         // Encoded per property type
```

**Example** - set uint8 property 3 (brightness) to 200:
```
C0 01 03 C8
│  │  │  └── value = 200 (uint8)
│  │  └───── property_id = 3
│  └──────── PROPERTY_UPDATE, flags=0
└─────────── fragment header: complete message
```

**Example** - set array<uint8,3> property 5 (color) to RGB(255, 0, 128):
```
C0 01 05 FF 00 80
│  │  │  └──────── value = [255, 0, 128] (3x uint8, no length prefix for ARRAY)
│  │  └─────────── property_id = 5
│  └────────────── PROPERTY_UPDATE, flags=0
└───────────────── fragment header: complete message
```

### Varint Encoding

Variable-length integer, 7 data bits per byte, MSB is continuation flag:

```
Value 0-127:     1 byte   [0xxxxxxx]
Value 128-16383: 2 bytes  [1xxxxxxx] [0xxxxxxx]
Value 16384+:    3+ bytes [1xxxxxxx] [1xxxxxxx] [0xxxxxxx] ...
```

```
0      → 0x00
127    → 0x7F
128    → 0x80 0x01
253    → 0xFD 0x01
16383  → 0xFF 0x7F
```

### PropId Encoding

Property/function ID, 1-2 bytes:

```
ID 0-127:      1 byte   [0xxxxxxx]           (high bit 0)
ID 128-32767:  2 bytes  [1xxxxxxx] [xxxxxxxx] (high bit 1, low 7 + high 8)
```

### Type Encoding for Values

| Type | ID | Wire Size | Encoding |
|------|----|-----------|----------|
| BOOL | 0x01 | 1 byte | 0x00=false, 0x01=true |
| INT8 | 0x02 | 1 byte | Signed byte |
| UINT8 | 0x03 | 1 byte | Unsigned byte |
| INT32 | 0x04 | 4 bytes | Little-endian signed 32-bit |
| FLOAT32 | 0x05 | 4 bytes | IEEE 754 little-endian |
| ARRAY | 0x20 | N * element_size | Packed values, no count (count from schema) |
| LIST | 0x21 | varint count + elements | Count prefix + packed values |
| OBJECT | 0x22 | sum of field sizes | Fields in schema order, no tags |

### PING / PONG

Send a ping to test connectivity (with fragment header):
```
Byte 0:  0xC0          // fragment header: complete message
Byte 1:  0x60          // opcode=6 (PING), flags=0 (request)
Byte 2+: varint        // payload (echo value)
```

Server responds with (inside a fragment):
```
Byte 0:  0xC0          // fragment header: complete message
Byte 1:  0x16          // opcode=6 (PING), flags=1 (is_response)
Byte 2+: varint        // same payload echoed back
```

### ERROR Message

```
Byte 0:  0xX7          // opcode=7 (ERROR), flags: bit0=schema_mismatch
u16:     error_code    // Little-endian error code
utf8:    message       // varint length + UTF-8 bytes
```

If `schema_mismatch` flag is set (flags bit0 = 1), the client should resync by sending HELLO again.

Error codes:
| Code | Name |
|------|------|
| 0x0000 | SUCCESS |
| 0x0001 | INVALID_OPCODE |
| 0x0002 | INVALID_PROPERTY_ID |
| 0x0004 | TYPE_MISMATCH |
| 0x0005 | VALIDATION_FAILED |
| 0x0006 | OUT_OF_RANGE |
| 0x0009 | PROTOCOL_VERSION_MISMATCH |

---

## Testing with nRF Connect

1. Open nRF Connect on Android/iOS
2. Scan for "SmartGarland"
3. Connect
4. Find service `e3a10001-...`
5. Enable notifications on TX characteristic (`e3a10003-...`) by tapping the triple-down-arrow icon
6. Write hex `C0 00 01 FD 01 2A 00 00` to RX characteristic (`e3a10002-...`)
7. Observe TX notifications (each has a 1-byte fragment header):
   - First notification: HELLO response (fragment header `0xC0`, then `0x10` HELLO opcode)
   - Following: SCHEMA_UPSERT (may span multiple fragments if large)
   - Final: PROPERTY_UPDATE batch
8. Write a property update to test: e.g., `C0 01 03 C8` to set property 3 to 200 (`C0` = complete fragment header)

## Debugging

Serial monitor shows all BLE protocol activity:
```
[INFO][ProtoBLE] MicroProto BLE server started
[INFO][ProtoBLE] Client connected (handle=1, slot=0)
[INFO][ProtoBLE] MTU updated: 517 (handle=1)
[INFO][ProtoBLE] HELLO from BLE device 0x0000002A, version 1
[INFO][ProtoBLE] Sent schema (12 properties) to BLE client 0
[INFO][ProtoBLE] Sent 12 property values to BLE client 0
[INFO][ProtoBLE] BLE client 0 sync complete
[INFO][ProtoBLE] Property 3 updated by BLE client 0
```
