# 🌈 LED Strip Controller

Animate your LEDs with programs. A tiny ESP32 runs your animations autonomously — no WiFi, no cloud, no phone needed. Power it from a battery and wear it. Or connect to WiFi for a web editor with live preview, add Bluetooth remotes, or go fully remote through a gateway.

## ✨ What You Get

**At its simplest**: an ESP32 and an LED strip. Flash your animations, power it up, it runs. No WiFi, no app, no configuration. Great for wearables, costumes, art pieces.

**With WiFi**: open the web app from any browser. You get a code editor, live controls, and a real-time animation preview — no app install needed.

**Write animations in a few lines:**

```lua
function draw()
    for i in led do
        led[i] = hsv(e.time * 100 + i * 5, 1, e.lum)
    end
end
```

This creates a flowing rainbow. `e.lum` is a brightness slider, `e.time` is a clock — the UI controls appear automatically. You can create multi-stage animations with transitions, use smooth randomness for organic effects, and see it all in real-time preview before it hits the LEDs.

**📱 Control from anything.** The same device works with:
- Web browser (phone or desktop) over WiFi
- Bluetooth LE remote controls and buttons
- Remote access through the gateway from anywhere
- Mobile apps (via MicroProto protocol)
- Smart home integration (WIP)

**🔒 No cloud required.** Everything runs locally. WiFi is optional — useful for editing, but not needed to run animations. Add a gateway for remote access when you want it — the device connects outward, so no port forwarding.

## 💡 LED Segments

LEDs aren't always a straight line. You might have a ring, a matrix panel, or multiple strips arranged in a shape. The segment system lets you define named groups — a ring of 24 LEDs, a strip of 60, a 8x8 matrix — position them on a 2D canvas, and address them by name in your code:

```lua
function draw()
    for i in ring do
        ring[i] = hsv(e.clr + i * 15, 1, e.lum)
    end
    ring:blur(3)

    for i in strip do
        strip[i] = hsv(e.time * 50 + i, 1, e.lum)
    end
end
```

Each segment supports transforms like `blur`, `fade`, `shift`, `mirror`. The web UI shows a drag-and-drop canvas editor where you can arrange, rotate, and resize segments, with live preview showing actual LED colors.

## 🎛️ Optional Hardware

The core is just an ESP32 and an LED strip. But you can add:
- **Rotary encoder** — physical brightness/animation control
- **Microphone** — audio-reactive animations using `e.volume`
- **Buttons** — cycle through animations
- **Bluetooth remotes** — pair BLE button devices for wireless control

All optional peripherals are auto-detected via build flags. The firmware only includes what you enable.

## 🧱 Built on Reusable Libraries

This project isn't a monolith — it's built on a set of independent, reusable libraries:

- **[MicroProto](lib/microproto/README.md)** — a binary property protocol for embedded systems. Devices declare typed properties with constraints and UI hints. Clients auto-generate UI from the schema. Supports WebSocket, BLE, and gateway transports through a single protocol engine. Works for any IoT device, not just LEDs.
- **WebUtils** — minimal HTTP server and request routing for ESP32, no async framework overhead
- **WiFiMan** — WiFi connection manager with captive portal, credential storage, and auto-reconnect

## 🌍 Remote Gateway

A Go service that lets you access your devices from anywhere. The device makes an outbound WebSocket connection to the gateway — no port forwarding, works behind any NAT.

```bash
cd gateway && go build ./cmd/gateway/ && ./gateway -addr :8080
```

The gateway shows all your registered devices with online/offline status. Click a device to get the full UI — same controls, same editor, same live preview, just proxied through the gateway. When no one is watching, the device goes idle and stops sending data to save bandwidth.

Multiple users can view the same device simultaneously. Authentication via device tokens.

## 🚀 Getting Started

```bash
# Build and upload firmware
pio run -e minic-v4 -t upload

# Open web UI
open http://led.local

# Optional: run gateway for remote access
cd gateway && go build ./cmd/gateway/ && ./gateway
```

## Board Configurations

| Environment | Board | Features |
|-------------|-------|----------|
| `minic-v4` | XIAO ESP32-S3 | Default, compact form factor |
| `gaslamp` | XIAO ESP32-S3 | Encoder + microphone |
| `xiao-ws2811` | XIAO ESP32-S3 | WS2811 strip type |
| `esp32-c3` | ESP32-C3 DevKit | Budget option |

## Project Layout

```
lib/microproto/        MicroProto protocol library
lib/webutils/          HTTP server and routing
lib/wifiman/           WiFi manager with captive portal
src/                   ESP32 firmware
widgets/               TypeScript UI widget library (shared)
gateway/               Go gateway service
test/                  Native, JS, and integration tests
```

## Documentation

- [MicroProto Protocol Spec](lib/microproto/README.md) — wire format, opcodes, type system
- [MicroProto Developer Guide](lib/microproto/CLAUDE.md) — architecture and API reference
- [BLE Transport](lib/microproto/transport/BLE.md) — Bluetooth LE details

## Tests

```bash
pio test -e native                          # C++ unit tests
node test/js/microproto-client.test.js      # JS client tests
python scripts/run_integration_tests.py     # Integration tests (needs device)
```
