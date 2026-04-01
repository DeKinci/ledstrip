# microproto-ws

MicroProto transport over WebSocket. Connects the MicroProto protocol engine to browser and native clients via the links2004/WebSockets library.

## Role

microproto-ws exists so that:

- Web browsers and other WebSocket clients can communicate with the device using the MicroProto binary protocol
- The protocol engine (MicroProtoController) stays transport-agnostic — this library is the WebSocket adapter
- Multiple transports (WebSocket + BLE) can run simultaneously, each registering with the same controller

This is the primary transport for development and web UIs. BLE (microproto-ble) serves the same role for mobile/constrained clients.

## What It Is Not

- Not the protocol — all protocol logic (handshake, schema sync, property updates, RPC) lives in microproto's `MicroProtoController`
- Not the web UI — that's microproto-web. This is just the binary transport pipe.

## Usage

```cpp
#include <MicroProtoServer.h>

MicroProto::MicroProtoServer ws(81);  // WebSocket on port 81

void setup() {
    ws.begin(&controller);  // registers as transport with MicroProtoController
}

void loop() {
    ws.loop();
}
```

## API

```cpp
MicroProtoServer(uint16_t port = 81);
void begin(MicroProtoController* controller);
void loop();
uint8_t connectedClients();
```

## Transport Interface

Implements `MicroProtoTransport`:

| Method | Behavior |
|--------|----------|
| `send(clientId, data, len)` | Binary WebSocket frame |
| `maxClients()` | Default 4 (configurable via `MICROPROTO_MAX_CLIENTS`) |
| `isClientConnected(clientId)` | Connection check |
| `maxPacketSize(clientId)` | 4096 bytes |
| `capabilities()` | Default (no BLE filtering) |

## Event Flow

```
Browser opens WebSocket to :81
  → WStype_CONNECTED  → controller.onClientConnected(globalId)
  → client sends HELLO (binary)
  → WStype_BIN        → controller.processMessage(globalId, data, len)
  → controller handles protocol, calls ws.send() for responses
  → WStype_DISCONNECTED → controller.onClientDisconnected(globalId)
```

Text messages are logged as warnings and ignored — MicroProto is binary-only.

## Dependencies

- microproto (MicroProtoTransport interface, MicroProtoController)
- links2004/WebSockets ^2.7.1
