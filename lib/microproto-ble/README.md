# microproto-ble

MicroProto transport over BLE GATT. Connects the MicroProto protocol engine to BLE clients (phones, tablets, other ESP32s) using microble's message service for fragmentation and connection management.

## Role

microproto-ble exists so that:

- BLE clients can use the same MicroProto protocol as WebSocket clients — same handshake, same properties, same RPC
- Large protocol messages are automatically fragmented to fit BLE MTU and reassembled on receive
- BLE and WebSocket transports coexist on the same controller — a phone over BLE and a browser over WebSocket see the same properties

This transport advertises `requiresBleExposed = true`, which tells the controller to only sync properties and functions marked `ble_exposed`. This keeps BLE traffic lean — not everything on the device needs to be accessible over BLE's limited bandwidth.

## What It Is Not

- Not the BLE stack — that's microble (NimBLE init, GATT service, fragmentation)
- Not the protocol — that's microproto's `MicroProtoController`
- Not a scanner/central — that's bleman. This is peripheral (server) mode only.

## Usage

```cpp
#include <MicroProtoBleServer.h>

MicroProto::MicroProtoBleServer ble;

void setup() {
    MicroBLE::init("MyDevice");
    ble.begin(&controller);  // registers as transport, sets up GATT service, starts advertising
}

void loop() {
    ble.loop();  // drains BLE RX queue, delivers messages to controller
}
```

## API

```cpp
void begin(MicroProtoController* controller);
void loop();
uint8_t connectedClients();
```

## Transport Interface

Implements `MicroProtoTransport`:

| Method | Behavior |
|--------|----------|
| `send(clientId, data, len)` | Auto-fragments via BleMessageService, sends as GATT notifications |
| `maxClients()` | 3 |
| `isClientConnected(clientId)` | Slot connection check |
| `maxPacketSize(clientId)` | Current MTU payload size for that client |
| `capabilities()` | `{ requiresBleExposed: true }` — only ble_exposed properties synced |

## GATT Service

Fixed UUIDs (hardcoded):

| Characteristic | UUID | Direction |
|---------------|------|-----------|
| Service | `e3a10001-...1d00` | — |
| RX | `e3a10002-...1d00` | Client → Device (write) |
| TX | `e3a10003-...1d00` | Device → Client (notify) |

## Threading

BLE writes arrive on the NimBLE task. `BleMessageService` queues reassembled messages in an atomic ring buffer. `loop()` drains the queue on the main loop and forwards complete messages to the controller. No protocol logic runs on the NimBLE task.

## Dependencies

- microproto (MicroProtoTransport interface, MicroProtoController)
- microble (BleMessageService, MicroBLE::init)
