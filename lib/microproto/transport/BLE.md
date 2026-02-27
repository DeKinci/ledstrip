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

### MTU Handling

- Default BLE MTU is 23 bytes (20 byte ATT payload after 3-byte overhead)
- The server tracks per-client MTU via `onMTUChange` callback
- All outgoing messages are checked against the client's MTU payload size
- Messages exceeding MTU are **dropped** (not truncated - truncation corrupts binary protocol messages)
- Schema is sent as individual per-property SCHEMA_UPSERT messages to fit within MTU
- Property values are batched up to the MTU limit, then a new batch starts
- The HELLO response `max_packet_size` field is set to the negotiated MTU payload size

### Thread Safety

BLE characteristic write callbacks execute on the NimBLE task (not the Arduino main loop). The server uses a single-producer single-consumer ring buffer (`_rxQueue`) to safely transfer incoming messages to the main task:

1. `onWrite()` (NimBLE task): copies message data into next ring buffer slot
2. `processRxQueue()` (main task, called from `loop()`): processes queued messages through the `MessageRouter`

### Current Scope

HELLO handshake + property sync + property updates + ping/pong. No RPC or resource operations over BLE yet.

**Note**: The spec suggests filtering to `ble_exposed` properties only. The current implementation sends all properties. This will be tightened in a future pass.

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
// NOW send HELLO
val hello = buildHelloRequest()
rxChar.value = hello
rxChar.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
gatt.writeCharacteristic(rxChar)

// In onCharacteristicChanged (TX notifications):
fun onCharacteristicChanged(gatt: BluetoothGatt, char: BluetoothGattCharacteristic) {
    val data = char.value
    parseMessage(data)  // Parse MicroProto binary message
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
// Send HELLO
let hello = buildHelloRequest()
peripheral.writeValue(hello, for: rxCharacteristic, type: .withResponse)

// Receive in didUpdateValueFor:
func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor char: CBCharacteristic, error: Error?) {
    guard let data = char.value else { return }
    parseMessage(data)
}
```

---

## MicroProto Binary Protocol Quick Reference

All messages start with a 1-byte operation header: `[opcode:4bits][flags:4bits]`.

All multi-byte integers are **little-endian**.

### Building a HELLO Request

```
Byte 0:  0x00          // opcode=0 (HELLO), flags=0 (request)
Byte 1:  0x01          // protocol_version = 1
Byte 2+: varint        // max_packet_size (use negotiated MTU - 3, e.g., 509 for MTU 512)
Byte N+: varint        // device_id (any unique uint32, e.g., hash of MAC address)
```

**Example** (MTU=256, device_id=42):
```
00 01 FD 01 2A
│  │  │     └── device_id = 42
│  │  └──────── max_packet_size = 253 (varint: 0xFD 0x01)
│  └─────────── protocol_version = 1
└────────────── opcode=HELLO, flags=0 (request)
```

### Parsing the HELLO Response

```
Byte 0:  0x10          // opcode=0 (HELLO), flags=1 (is_response)
Byte 1:  0x01          // protocol_version (must be 1)
Byte 2+: varint        // max_packet_size (server's max send size)
Byte N+: varint        // session_id
Byte M+: varint        // server_timestamp (seconds since boot)
```

### Parsing SCHEMA_UPSERT

Over BLE, schema arrives as individual (non-batched) messages, one per property:

```
Byte 0:  0x03          // opcode=3 (SCHEMA_UPSERT), flags=0 (not batched)
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

To update a property value, write to the RX characteristic:

```
Byte 0:  0x01          // opcode=1 (PROPERTY_UPDATE), flags=0 (single, no timestamp)
propid:  property_id   // 1-2 bytes (from schema)
bytes:   value         // Encoded per property type
```

**Example** - set uint8 property 3 (brightness) to 200:
```
01 03 C8
│  │  └── value = 200 (uint8)
│  └───── property_id = 3
└──────── PROPERTY_UPDATE, flags=0
```

**Example** - set array<uint8,3> property 5 (color) to RGB(255, 0, 128):
```
01 05 FF 00 80
│  │  └──────── value = [255, 0, 128] (3x uint8, no length prefix for ARRAY)
│  └─────────── property_id = 5
└────────────── PROPERTY_UPDATE, flags=0
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

Send a ping to test connectivity:
```
Byte 0:  0x60          // opcode=6 (PING), flags=0 (request)
Byte 1+: varint        // payload (echo value)
```

Server responds with:
```
Byte 0:  0x16          // opcode=6 (PING), flags=1 (is_response)
Byte 1+: varint        // same payload echoed back
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
6. Write hex `00 01 FD 01 2A` to RX characteristic (`e3a10002-...`)
7. Observe TX notifications:
   - First notification: HELLO response (starts with `0x10`)
   - Following notifications: SCHEMA_UPSERT messages (start with `0x03`)
   - Final notifications: PROPERTY_UPDATE batches (start with `0x11`)
8. Write a property update to test: e.g., `01 03 C8` to set property 3 to 200

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
