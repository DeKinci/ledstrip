# bleman

BLE central (client) role: discover, connect to, and manage external BLE peripherals. The counterpart to microble (which handles the peripheral/server role).

## Role

bleman exists so that:

- The device can interact with BLE peripherals (buttons, sensors, lights) as a central
- Each peripheral type has a dedicated driver with its own connection logic
- Drivers are pluggable — register a factory, bleman handles scanning, connecting, and driver lifecycle
- Known devices persist across reboots and auto-reconnect when in range
- The whole system is manageable via HTTP API and web UI

bleman handles the **central role** — it scans for and connects to *other* BLE devices. microble handles the **peripheral role** — it exposes GATT services that *other* devices connect to. They coexist on the same NimBLE stack.

## What It Is Not

- Not a GATT server — that's microble
- Not a generic BLE library — it's specifically a device manager with driver dispatch
- Not hardware-specific — drivers abstract the peripheral protocol, bleman handles the lifecycle

## Device Lifecycle

```
Background scan finds device
    ↓
User adds to known devices (via API or code) with a type
    ↓
Device seen in scan + autoConnect → connection attempt
    ↓
Driver factory called for device type → driver allocated from static pool
    ↓
driver.init(client) — subscribe to characteristics, return false to reject
    ↓
driver.loop() — called every iteration while connected
    ↓
Disconnect → driver.deinit() — cleanup, return to pool
```

## Driver System

Each peripheral type needs a driver:

```cpp
class MySensorDriver : public BleDriver {
    bool init(NimBLEClient* client) override {
        // discover services, subscribe to characteristics
        return true;  // false = reject connection
    }
    void loop() override {
        // poll, process notifications, emit events
    }
    void deinit() override {
        // cleanup, return self to static pool
    }

    // Static pool (no heap allocation)
    static std::array<MySensorDriver, 4> _pool;
    static MySensorDriver* allocate() { /* find unused slot */ }
};
```

Register with bleman:

```cpp
ble.registerDriver("sensor", [](const BleKnownDevice& dev) -> BleDriver* {
    return MySensorDriver::allocate();
});
```

### Built-in: BleButtonDriver

HID button driver with gesture detection via microinput:

```cpp
ble.registerDriver("button", [](const BleKnownDevice& dev) -> BleDriver* {
    auto* d = BleButtonDriver::allocate();
    if (d) d->setActionCallback([](SequenceDetector::Action action) {
        // SINGLE_CLICK, DOUBLE_CLICK, HOLD_TICK, CLICK_HOLD_TICK, HOLD_END
    });
    return d;
});
```

## Usage

```cpp
BleMan::BleManager ble(&httpDispatcher);

void setup() {
    ble.registerDriver("button", buttonFactory);
    ble.begin();  // loads known devices from NVS, starts background scan
}

void loop() {
    ble.loop();  // scans, connects, runs drivers
}
```

## Device Management

```cpp
ble.addKnownDevice("AA:BB:CC:DD:EE:FF", "My Button", "button", "button", true);
ble.removeKnownDevice("AA:BB:CC:DD:EE:FF");
ble.connectToDevice("AA:BB:CC:DD:EE:FF");
ble.disconnectDevice("AA:BB:CC:DD:EE:FF");
```

Known devices persist to NVS. Auto-reconnect for devices with `autoConnect = true`.

## Scanning

```cpp
ble.triggerScanNow();       // one-time active scan
ble.startBackgroundScan();  // continuous passive scan
ble.stopBackgroundScan();
```

## HTTP API

When constructed with an `HttpDispatcher*`:

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/bleman` | Web UI |
| GET | `/bleman/types` | Registered driver types |
| POST | `/bleman/scan` | Start scan |
| GET | `/bleman/scan/results` | Results + status |
| GET | `/bleman/known` | Known devices |
| POST | `/bleman/known` | Add/update device |
| DELETE | `/bleman/known/{addr}` | Remove device |
| GET | `/bleman/connected` | Connected devices |
| POST | `/bleman/connect/{addr}` | Connect |
| POST | `/bleman/disconnect/{addr}` | Disconnect |

## Limits

All configurable via defines:

| Define | Default | Description |
|--------|---------|-------------|
| `BLEMAN_MAX_KNOWN_DEVICES` | 8 | Persistent device slots |
| `BLEMAN_MAX_CONNECTED_DEVICES` | 4 | Concurrent connections |
| `BLEMAN_MAX_SCAN_RESULTS` | 32 | Cached scan results |
| `BLEMAN_MAX_DRIVER_TYPES` | 8 | Registered driver factories |
