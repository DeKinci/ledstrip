# microlog

The universal observability layer. Every library in the system can log and expose metrics through microlog without pulling in any other dependency.

## Role

microlog is the **foundation of observability**. It exists so that:

- Any library can emit structured logs without knowing where they go
- Any library can expose metrics without knowing who consumes them
- The application decides where logs and metrics are routed (serial, network, flash, cloud) by plugging in sinks
- Desktop and device builds both work — macros adapt automatically

microlog has **zero dependencies**. It sits at the bottom of the dependency graph so everything can use it.

## What It Is Not

- Not a transport — it doesn't know about WebSockets, BLE, or HTTP. That's what `microlog-up` (MicroProto bridge) is for.
- Not a storage system — sinks can persist logs, but microlog itself is fire-and-forget.
- Not a filtering framework — all levels are always emitted. Sinks decide what to keep.

## Logging

```cpp
#include <MicroLog.h>

LOG_INFO("WiFi", "Connected to %s", ssid);
LOG_WARN("Proto", "Unknown property ID %d", propId);
LOG_ERROR("Storage", "Write failed: %zu bytes", len);
LOG_DEBUG("BLE", "Scan result: %s", addr);  // DEBUG builds only
```

Printf-style formatting. On Arduino, output includes `HH:MM:SS.mmm` timestamps. On desktop, prints to stdout (DEBUG is a no-op).

## Sinks

The application decides where logs go by registering sinks:

```cpp
class CloudSink : public MicroLog::Sink {
    void onLog(MicroLog::Level level, const char* tag, const char* message) override {
        // buffer to flash, stream over network, etc.
    }
};

MicroLog::Logger::addSink(&cloudSink);  // up to 4 sinks
```

This is how `microlog-up` works — it registers a sink that routes logs into a MicroProto `StreamProperty`, making them available to connected clients.

## Metrics

Metrics auto-register on construction and print periodically as a single line:

```
heap=125000B rssi=-45dBm http_reqs=42
```

### Gauge — current value

```cpp
static MicroLog::Gauge<uint32_t> mHeap("heap", "B", /*debounceMs=*/5000);

void loop() {
    mHeap.set(ESP.getFreeHeap());
    MicroLog::Logger::loop();  // prints all metrics at interval
}

// Optional: push callback on value change
mHeap.onPush([](uint32_t timestamp, uint32_t value) { });
```

### Counter — cumulative total

```cpp
static MicroLog::Counter<uint32_t> mRequests("http_reqs");

void handleRequest() {
    mRequests.increment();
}
```

## Configuration

```cpp
MicroLog::Logger::setMetricsInterval(10000);  // default 30s
```

## Limits

| Resource | Max |
|----------|-----|
| Sinks | 4 |
| Metrics | 16 |
