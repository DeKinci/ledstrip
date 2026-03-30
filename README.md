# LED Strip Controller

A programmable LED controller built on ESP32 that treats LED animations like shader programs. Write animations in Lua, edit them live from a web browser, organize LEDs into named segments, and access everything remotely through a gateway.

## How It Works

You write small Lua programs that run every frame and paint colors onto LED segments:

```lua
function draw()
    for i in ring do
        ring[i] = hsv(e.clr + e.time * 100 * e.spd + i * 5, 1, e.lum)
    end
end
```

The device serves a web UI where you can edit shaders, adjust parameters with sliders, arrange LED segments on a canvas, and see a live preview — all generated automatically from the properties the firmware declares. No hardcoded UI.

A Go gateway service lets you access devices remotely. The device connects outward to the gateway (no port forwarding needed), and the gateway proxies everything — including the auto-generated UI.

## What's Interesting

**Animations are code, not configuration.** Lua shaders run in a sandboxed VM with coroutine support, so you can write sequential multi-phase animations (`over`, `wait`, `frame`) without state machines. The editor runs on the device itself.

**The UI is derived from the protocol.** The device declares typed properties with constraints and widget hints. The client renders appropriate controls — sliders, toggles, code editors, canvas layouts — purely from the schema. Adding a new property to the firmware automatically produces a new UI element.

**One protocol, many transports.** MicroProto is a compact binary protocol with a single engine (`MicroProtoController`) that handles all the logic. WebSocket, BLE, and gateway connections are thin wrappers. The gateway doesn't parse protocol messages — it just activates the device when a user connects and forwards bytes.

**LED segments are first-class objects.** You can define lines, rings, and matrices, position them in 2D, and address them by name in Lua. Each segment supports transforms like `blur`, `fade`, `shift`, `mirror`. The web canvas editor lets you drag and rotate them.

**The widget library ships what you need.** TypeScript widgets are tree-shaken at build time. The device firmware embeds only the widgets it uses. The gateway embeds all of them. Same source code, different build profiles.

## Project Layout

```
lib/microproto/        Protocol library (properties, schema, binary encoding, transports)
src/                   ESP32 firmware (animations, Lua VM, segments, web server)
widgets/               Shared TypeScript widget library
web/                   Device-side widget build profile
gateway/               Go gateway service (auth, device registry, WS proxy)
test/                  Native C++ tests, JS client tests, integration tests
```

## Getting Started

```bash
# Build and upload firmware
pio run -e minic-v4 -t upload

# Open web UI
open http://led.local

# Run gateway (optional, for remote access)
cd gateway && go build ./cmd/gateway/ && ./gateway -addr :8080
```

## Board Configurations

| Environment | Board | Notes |
|-------------|-------|-------|
| `minic-v4` | XIAO ESP32-S3 | Default, D10 LED pin |
| `gaslamp` | XIAO ESP32-S3 | Encoder + mic input |
| `xiao-ws2811` | XIAO ESP32-S3 | WS2811 strip |
| `esp32-c3` | ESP32-C3 DevKit | Basic config |

## Further Reading

- [MicroProto Protocol Spec](lib/microproto/README.md) — wire format, opcodes, type system
- [MicroProto Developer Guide](lib/microproto/CLAUDE.md) — architecture and API reference
- [BLE Transport](lib/microproto/transport/BLE.md) — BLE-specific details

## Tests

```bash
pio test -e native                          # C++ unit tests
node test/js/microproto-client.test.js      # JS client tests
python scripts/run_integration_tests.py     # Integration (needs device)
```
