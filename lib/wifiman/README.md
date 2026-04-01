# wifiman

Owns the WiFi lifecycle completely. After `begin()`, the application shouldn't think about WiFi — wifiman handles scanning, connecting, reconnecting, fallback to AP mode, and user configuration via captive portal.

## Role

wifiman exists so that:

- The application calls `begin()` and `loop()` and gets a working network connection
- Multiple networks are supported with priority-based selection — only networks actually in range are attempted
- When nothing works, users can configure WiFi through a captive portal without reflashing
- Credentials persist across reboots in NVS
- Connection state is exposed via callbacks so the app can react (start services on connect, pause on disconnect)

wifiman registers its routes on an `HttpDispatcher` — it doesn't own the HTTP server.

## What It Is Not

- Not a network stack — it manages WiFi association and IP acquisition, not sockets or protocols
- Not a provisioning system — no BLE provisioning, no SmartConfig. Just scan + captive portal.
- Not blocking — every state transition happens in `loop()` via a non-blocking state machine (< 1ms per call)

## Quick Start

```cpp
WiFiMan::WiFiManager wifi(&httpDispatcher);

void setup() {
    wifi.setAPCredentials("MyDevice-Setup");
    wifi.setHostname("mydevice");
    wifi.credentials().addNetwork("HomeWiFi", "pass", 100);
    wifi.begin();
}

void loop() {
    wifi.loop();
}
```

## Connection Flow

1. **SCANNING** — async scan for what's actually in range
2. **Sorting** — saved networks ranked by priority > last-connected > RSSI
3. **CONNECTING** — tries each matching network in order
4. **CONNECTED** — monitors link, auto-retries on drop
5. **FAILED** — all attempts exhausted, retries after delay or falls back to AP mode

No time is wasted attempting networks that aren't broadcasting.

## States

| State | Meaning |
|-------|---------|
| `IDLE` | Not started or manually stopped |
| `SCANNING` | Async network scan in progress |
| `CONNECTING` | Attempting association |
| `CONNECTED` | Link up, IP acquired |
| `AP_MODE` | Access point running, captive portal active |
| `FAILED` | All attempts failed, will retry |

```cpp
wifi.isConnected();
wifi.isAPMode();
wifi.getIP();
wifi.getCurrentSSID();
wifi.getLastError();  // "Wrong password", "Network not found", etc.
```

## Credentials

```cpp
auto& creds = wifi.credentials();
creds.addNetwork("Home", "pass", /*priority=*/100);
creds.addNetwork("Work", "pass", /*priority=*/50);
creds.removeNetwork("Old");
creds.clearAll();
```

Up to 10 networks. Stored in NVS with RSSI history and last-connected timestamps.

## Callbacks

```cpp
wifi.onConnected([](const String& ssid) { /* start services */ });
wifi.onDisconnected([]() { /* pause services */ });
wifi.onAPStarted([](const String& ssid) { /* portal is live */ });
wifi.onAPClientConnected([](uint8_t n) { /* n clients on AP */ });
```

## Configuration

```cpp
wifi.setConnectionTimeout(15000);  // per-network attempt (default 15s)
wifi.setRetryDelay(5000);          // between retry cycles (default 5s)
wifi.setAPTimeout(0);              // AP auto-shutdown, 0 = never
```

## Captive Portal

In AP mode, wifiman intercepts captive portal detection requests (`/generate_204`, `/hotspot-detect.html`, etc.) and serves a configuration UI. REST API:

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/wifiman` | Configuration UI |
| GET | `/wifiman/status` | State, IP, SSID, errors |
| GET | `/wifiman/scan` | Available networks |
| GET | `/wifiman/list` | Saved credentials |
| POST | `/wifiman/add` | Add `{ssid, password?, priority?}` |
| POST | `/wifiman/remove` | Remove `{ssid}` |
| POST | `/wifiman/clear` | Clear all |
| POST | `/wifiman/connect` | Trigger connection from AP mode |

## Manual Control

```cpp
wifi.startAP();     // force AP mode
wifi.stopAP();
wifi.disconnect();  // go idle
wifi.retry();       // retry from FAILED/IDLE
```
