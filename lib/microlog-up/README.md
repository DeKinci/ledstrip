# microlog-up

Bridges microlog and MicroProto. Turns log output into streamable properties so connected clients can watch logs in real time and review persistent errors after reboot.

## Role

microlog-up exists so that:

- Logs are observable remotely — not just on serial. Any MicroProto client (web UI, gateway, phone) can see live logs and stored errors.
- The device tracks boot count and persistent error history across reboots without any application code
- The logging system stays decoupled — microlog itself knows nothing about MicroProto. This library is the glue.

It registers a `Sink` with microlog and routes messages into two `StreamProperty` instances that MicroProto syncs to clients like any other property.

## What It Is Not

- Not a log storage backend — it uses MicroProto's existing property persistence for error history, not a separate log file system
- Not a filter — all levels stream live, warnings and errors persist. The client decides what to show.

## Usage

```cpp
#include <MicroLogProto.h>

void setup() {
    MicroLogProto::init();  // initializes Logger + ProtoSink, increments boot count
}
```

That's it. From this point, all `LOG_*` calls are captured and available to MicroProto clients.

## What It Creates

Two stream properties, auto-registered in the MicroProto schema:

| Property | Path | Persisted | Content |
|----------|------|-----------|---------|
| `sys/errorLog` | Visible | Yes | Warnings and errors (ring buffer, last N entries survive reboot) |
| `sys/logStream` | Hidden | No | All log levels, live-only (no history) |

Both carry `LogEntry` structs (100 bytes each):

```cpp
struct LogEntry {
    uint32_t timestamp;          // millis()
    uint16_t bootCount;          // incremented each boot
    uint8_t  level;              // DEBUG/INFO/WARN/ERROR
    uint8_t  _reserved;
    std::array<char, 12> tag;    // e.g. "WiFi", "BLE"
    std::array<char, 80> message;
};
```

## Configuration

```cpp
// Error log buffer depth (default 10 entries)
#define MICROLOG_BUFFER_SIZE 20
```

## Dependencies

- microlog (sink interface)
- microproto (StreamProperty)
