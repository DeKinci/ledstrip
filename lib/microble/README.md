# microble

Owns the BLE peripheral stack. Provides shared NimBLE server infrastructure that multiple GATT services can coexist on — the BLE equivalent of what `HttpDispatcher` is for HTTP.

## Role

microble exists so that:

- Multiple protocols can expose GATT services on the same BLE server without conflicting (MicroProto, Matter, custom services)
- Stack initialization is idempotent — any library can call `init()` without worrying about order or double-init
- Connection slot tracking, MTU negotiation, and fragmentation are handled once, not reimplemented per protocol
- The NimBLE threading model is abstracted — writes arrive on the NimBLE task but are queued and delivered on the main loop

microble is **peripheral (server) only**. For central (client) role — scanning and connecting to external BLE devices — see `bleman`.

## What It Is Not

- Not a protocol — it provides GATT transport infrastructure. Protocols like MicroProto or Matter build on top.
- Not an application-level API — it's plumbing. Applications interact with the protocol libraries that use microble.
- Not a scanner or connector — that's bleman's job (central role).

## Layers

```
Your protocol (MicroProto, Matter, custom)
    ↓ implements MessageHandler or GattHandler
BleMessageService — message framing, fragmentation, cross-task queueing
    ↓
BleGattService — GATT characteristic management, client slot tracking
    ↓
MicroBLE::init() — shared NimBLE server, advertising
    ↓
NimBLE stack
```

Use `BleMessageService` when your protocol sends discrete messages. Use `BleGattService` directly for raw characteristic-level access.

## Stack Init

```cpp
MicroBLE::init("MyDevice", /*txPower=*/9);  // idempotent
MicroBLE::startAdvertising();
```

Multiple libraries can call `init()` — only the first call creates the server.

## BleGattService — raw GATT access

Manages one GATT service with RX/TX characteristics and per-connection client slots (up to 8).

```cpp
GattConfig config {
    .serviceUUID = "...",
    .rxUUID      = "...",   // client writes here
    .txUUID      = "...",   // server notifies/indicates here
    .txIndicate  = false,   // false = NOTIFY, true = INDICATE
    .maxClients  = 3
};

class MyHandler : public GattHandler {
    void onConnect(uint8_t slot) override { }
    void onDisconnect(uint8_t slot) override { }
    void onMTUChange(uint8_t slot, uint16_t mtu) override { }
    void onWrite(uint8_t slot, const uint8_t* data, size_t len) override {
        // NimBLE task — keep fast, queue work for main loop
    }
};

BleGattService gatt;
gatt.begin(&handler, config);
gatt.send(slot, data, len);
```

## BleMessageService — message framing

Wraps BleGattService with automatic fragmentation and reassembly. Incoming writes are queued from the NimBLE task and delivered on the main loop via `MessageHandler::onMessage()`.

```cpp
class MyProtocol : public MessageHandler {
    void onConnect(uint8_t slot) override { }
    void onDisconnect(uint8_t slot) override { }
    void onMessage(uint8_t slot, const uint8_t* data, size_t len) override {
        // Complete reassembled message, on main loop
    }
};

BleMessageService</*MaxMsg=*/4096, /*MaxClients=*/3, /*QueueDepth=*/4> svc;
svc.begin(&protocol, config);

void loop() {
    svc.loop();                          // drains RX queue → onMessage()
    svc.sendMessage(slot, data, len);    // auto-fragments to fit MTU
}
```

## Fragmentation Protocol

1-byte header per BLE packet:

| Header | Meaning |
|--------|---------|
| `0xC0` | Complete (single packet) |
| `0x80` | First fragment |
| `0x00` | Middle fragment |
| `0x40` | Last fragment |

Payload per fragment: `MTU - 3 (ATT) - 1 (header)` bytes.

## Threading Model

BLE writes arrive on the **NimBLE task**. This is a different FreeRTOS task from your main loop.

- `BleGattService` uses spinlocks for slot management
- `BleMessageService` uses an atomic ring buffer to queue received messages for the main loop
- `GattHandler::onWrite()` runs on the NimBLE task — keep it fast (just queue)
- `MessageHandler::onMessage()` runs on the main loop — safe to do real work
