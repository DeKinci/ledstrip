# microproto-gw

MicroProto gateway client transport. Connects the device to a remote gateway server over an outbound WebSocket, enabling remote access and cross-device coordination without direct network visibility.

## Role

microproto-gw exists so that:

- Devices behind NAT or on different networks can be reached through a centralized gateway
- The gateway acts as a multiplexing proxy — one WebSocket connection carries all protocol traffic for the device
- Remote clients (web dashboards, CLI tools) interact with the device through the gateway using the same MicroProto protocol
- The device registers itself with device ID, name, and IP for discovery

Like microproto-ws and microproto-ble, this is a transport adapter. It implements `MicroProtoTransport` so the controller handles all protocol logic.

## What It Is Not

- Not the gateway server — that's a separate Go service. This is the device-side client that connects to it.
- Not the protocol — all protocol logic lives in microproto's `MicroProtoController`
- Not a local transport — microproto-ws (WebSocket) and microproto-ble (BLE) handle local clients. This handles the remote uplink.

## Usage

```cpp
#include <GatewayClient.h>

void setup() {
    controller.begin();
    GatewayClient::init(&controller);  // connects to gateway, registers transport
}

void loop() {
    GatewayClient::loop();
}
```

## Configuration

Gateway connection is configured via persistent MicroProto properties:

| Property | Type | Description |
|----------|------|-------------|
| `gatewayUrl` | string (128) | Gateway WebSocket URL (e.g. `wss://gw.example.com/ws/device`) |
| `gatewayToken` | string (64) | Registration token for authentication |
| `deviceName` | string (32) | Human-readable device name (defaults to `LED-<mac-suffix>`) |

Build-time defaults can be set via `-DGATEWAY_URL=...` and `-DGATEWAY_TOKEN=...`. The property values take precedence if set.

## API

```cpp
namespace GatewayClient {
    void init(MicroProto::MicroProtoController* controller);
    void loop();
    bool isConnected();
}
```

## Transport Interface

Implements `MicroProtoTransport` internally:

| Method | Behavior |
|--------|----------|
| `send(clientId, data, len)` | Binary WebSocket frame to gateway |
| `maxClients()` | 1 (the gateway connection) |
| `isClientConnected(clientId)` | WebSocket connection state |
| `maxPacketSize(clientId)` | 4096 bytes |

## Connection Flow

```
Device boots, reads gatewayUrl property (or GATEWAY_URL define)
  → WebSocketsClient connects to gateway URL with query params:
      ?token=<token>&id=<mac>&name=<name>&ip=<local-ip>
  → On connect: sends idle HELLO (registers without full property sync)
  → Gateway activates device when a remote client requests it
  → Full HELLO handshake triggers normal schema + property sync
  → Auto-reconnects on disconnect (5 second interval)
```

## SSL

URLs starting with `wss://` or `https://` automatically use SSL. Port defaults to 443 for SSL, 80 otherwise.

## Dependencies

- microproto (MicroProtoTransport interface, MicroProtoController)
- links2004/WebSockets ^2.7.1
