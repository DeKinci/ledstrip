# LED Strip Project - Development Notes

## Project Overview

ESP32-based LED strip controller with web interface and WebSocket communication.

## Coding Conventions

### Memory & Allocation

Avoid heap allocations where possible. Prefer stack and static allocation:

- **Fixed-size arrays**: Use `std::array<T, N>` instead of C-style `T arr[N]`
- **Function callbacks**: Use `MicroFunction<Signature, CaptureSize>` instead of `std::function`
- **Strings**: Prefer `const char*` for static strings, `String` only when dynamic content needed
- **Containers**: Avoid `std::vector` in hot paths; use fixed-size `std::array` when size is known

```cpp
// Good
std::array<bool, 4> _clientReady = {};
MicroFunction<void(const DirtySet&), 8> callback;

// Avoid
bool _clientReady[4];           // Use std::array
std::function<void()> callback; // Heap allocates captures
```

### MicroFunction

Lightweight callable wrapper from `lib/MicroCore/MicroFunction.h`:

```cpp
// Zero-capture (function pointer only, 4 bytes)
MicroFunction<void(), 0> simple_cb;

// Small captures (e.g., `this` pointer, 8 bytes)
MicroFunction<void(int), 8> with_capture;

// Compile-time error if lambda captures exceed specified size
```

### Constants

- Use `static constexpr` for class constants
- Use `#define` for configurable limits that users may override

```cpp
// In header
#ifndef MYLIB_MAX_ITEMS
#define MYLIB_MAX_ITEMS 8
#endif

class MyClass {
    static constexpr uint32_t TIMEOUT_MS = 1000;
    std::array<Item, MYLIB_MAX_ITEMS> _items;
};
```

### Logging

Use `microlog` (`LOG_INFO`, `LOG_WARN`, `LOG_ERROR`) for all output. Never use `Serial.print` directly in library code.

```cpp
#include <MicroLog.h>

LOG_INFO("WiFi", "Connected to %s", ssid);
LOG_WARN("Proto", "Unknown property ID %d", propId);
LOG_ERROR("Storage", "Write failed: %zu bytes", len);
LOG_DEBUG("BLE", "Scan result: %s", addr);  // Only in DEBUG builds
```

Metrics use `Gauge<T>` and `Counter<T>` — they auto-register and print periodically:

```cpp
#include <Gauge.h>

static MicroLog::Gauge<uint32_t> mHeap("heap", "B", 5000);
static MicroLog::Counter<uint32_t> mRequests("http_reqs");

void loop() {
    mHeap.set(ESP.getFreeHeap());
    MicroLog::Logger::loop();  // Prints all metrics at configured interval
}
```

Library composition:
- `microlog` — Serial logging + metrics. Zero dependencies.
- `microlog-up` — Adds MicroProto streaming. `MicroLogProto::init()` handles everything.

### Non-Blocking Code

Never block in `loop()`. ESP32 is single-threaded — blocking means WiFi, WebSocket, BLE, and other services stop. Use non-blocking state machines that return after each unit of work.

## Current Architecture

Migrated from AsyncWebServer (poor performance) to **minimal synchronous WiFiServer + WebSockets**:
- `WiFiServer` on port 80 - direct TCP, no async framework overhead
- `WebSocketsServer` on port 81 - real-time LED updates

### Key Files

- `src/main.cpp` - Main prototype with HTTP server loop
- `lib/webutils/HttpRequest.h/.cpp` - HTTP request parser (has `operator bool()`, `HttpRequest::invalid()`)
- `lib/webutils/HttpRequestReader.h/.cpp` - Blocking request reader with body support
- `lib/webutils/HttpResponse.h/.cpp` - Response data container with static factories
- `lib/webutils/HttpResponseWriter.h/.cpp` - Writes HttpResponse to WiFiClient

## Web Server Design

### Current State

Blocking reader with full header + body support. Returns `HttpRequest` - use `if (!req)` to check validity. On failure, sends 408/400 and closes connection.

### Target Design: AsyncHttpReader

Non-blocking state-machine reader that accumulates data across loop iterations:

```cpp
class AsyncHttpReader {
    enum State { IDLE, READING_HEADERS, READING_BODY, COMPLETE, ERROR };
    State _state = IDLE;
    String _buffer;
    int _contentLength = 0;
    int _bodyBytesRead = 0;
    uint32_t _startTime = 0;

public:
    struct Config {
        uint32_t firstByteTimeoutMs = 5000;
        uint32_t readTimeoutMs = 500;
        size_t maxBodySize = 4096;
    };

    void begin(const Config& config = Config());  // Reset for new request
    bool process(WiFiClient& client);  // Non-blocking read, call each loop
    bool isComplete() const { return _state == COMPLETE; }
    bool hasError() const { return _state == ERROR; }
    bool getRequest(HttpRequest& req);  // Parse buffer into request
};
```

#### State Machine Flow

1. **IDLE** → `begin()` called → **READING_HEADERS**
2. **READING_HEADERS** → reads available bytes, looking for `\r\n\r\n`
   - Found terminator → extract Content-Length → **READING_BODY** (or COMPLETE if no body)
   - Timeout → **ERROR**
3. **READING_BODY** → reads available bytes until contentLength reached
   - All bytes read → **COMPLETE**
   - Timeout → **ERROR**
4. **COMPLETE** → `getRequest()` parses buffer into HttpRequest
5. **ERROR** → caller handles (send 408, close connection)

#### Integration Pattern

```cpp
void loop() {
    webSocket.loop();  // Always service WebSocket

    WiFiClient client = server.accept();
    if (client) {
        client.setNoDelay(true);
        reader.begin();

        // Non-blocking read loop
        while (client.connected() && !reader.isComplete() && !reader.hasError()) {
            reader.process(client);
            webSocket.loop();  // Stay responsive during read
            yield();
        }

        if (reader.isComplete()) {
            HttpRequest req;
            reader.getRequest(req);
            handleRequest(req, client);
        } else {
            client.print("HTTP/1.1 408 Request Timeout\r\n\r\n");
        }
        client.stop();
    }
}
```

### Why Not Coroutines?

- ESP32 Arduino has limited C++20 coroutine support
- State machine is explicit, portable, easy to debug
- No special compiler features needed

## Testing

### Device IP Discovery

```bash
# Find device by MAC address (shown during upload)
arp -a | grep -i "b8:f8:62"
```

### Manual Endpoint Testing

```bash
# Test ping
curl -w "\nTime: %{time_total}s\n" http://<IP>/ping

# Test index page
curl -s -o /dev/null -w "Status: %{http_code}, Size: %{size_download}b, Time: %{time_total}s\n" http://<IP>/

# Test 404
curl -w "\nStatus: %{http_code}\n" http://<IP>/notfound

# Test POST with body (once implemented)
curl -X POST -H "Content-Type: application/json" -d '{"name":"test"}' http://<IP>/api/shader
```

### Test Structure

```
test/
├── js/                      # JavaScript unit tests
│   └── microproto-client.test.js
├── integration/             # Python integration tests (run against device)
│   ├── __init__.py          # Shared: Colors, TestResult, discover_esp32
│   ├── test_http.py         # HTTP reliability + POST echo
│   ├── test_websocket.py    # WebSocket reliability
│   ├── test_parallel.py     # Parallel HTTP + WS stress test
│   ├── test_microproto.py   # MicroProto WebSocket protocol
│   └── test_wifiman.py      # WiFi management API
├── native/                  # Native C++ tests (run on laptop)
│   ├── mocks/               # Mock Arduino/ArduinoJson for native builds
│   ├── test_dispatcher/
│   ├── test_http/
│   ├── test_messages/
│   ├── test_microfunction/
│   └── test_wire/
└── onboard/                 # Tests that run on ESP32 device
    ├── test_http_request/
    ├── test_property_basic/
    ├── test_property_callbacks/
    ├── test_property_storage/
    └── test_property_system/

scripts/
├── run_integration_tests.py  # Integration test runner
└── pio_integration.py        # PlatformIO hook for `pio run -t test_device`
```

### Integration Tests (Python)

Auto-discovers ESP32 via ARP and runs HTTP/WebSocket/MicroProto tests.

#### Setup

```bash
# Create virtual environment (one time)
python3 -m venv .venv

# Install dependencies
.venv/bin/pip install requests websockets aiohttp
```

#### Running Tests

```bash
# Run all integration tests (auto-discovers device)
python scripts/run_integration_tests.py

# Run with specific IP
python scripts/run_integration_tests.py 192.168.1.100

# Run via PlatformIO
pio run -t test_device
```

#### Adding New Integration Tests

1. Create `test/integration/test_myfeature.py`:

```python
from . import Colors, TestResult

def test_my_feature(ip: str) -> TestResult:
    print(f"\n{Colors.CYAN}{Colors.BOLD}[TEST] My Feature{Colors.RESET}")
    result = TestResult("My Feature", True)

    # Run test logic...
    result.success_count += 1
    result.times.append(elapsed_ms)

    return result
```

2. Import and add to `scripts/run_integration_tests.py`:

```python
from integration.test_myfeature import test_my_feature

def run_test_suite(ip: str) -> List[TestResult]:
    results = []
    # ... existing tests ...
    results.append(test_my_feature(ip))
    return results
```

### Native Unit Tests (C++)

Tests for webutils, MicroProto wire format, MicroCore, etc. Run locally without hardware.

```bash
# Run all native tests
pio test -e native

# Run specific test
pio test -e native -f native/test_microfunction

# Run with verbose output
pio test -e native -v
```

### JavaScript Unit Tests

Tests for MicroProto JS client using MockWebSocket.

```bash
# Run from project root
node test/js/microproto-client.test.js
```

### Build & Upload

```bash
# Build
pio run -e seeed_xiao_esp32s3

# Upload
pio run -e seeed_xiao_esp32s3 -t upload

# Monitor serial
pio device monitor --baud 115200
```

## Performance Targets

- `/ping` latency: < 50ms average
- Reliability: 100% success rate under normal conditions
- WebSocket must stay responsive during HTTP handling

## Board Configurations

- **seeed_xiao_esp32s3** - Primary development board (8MB flash)
- **esp32-c3-devkitc-02** - Alternative board (4MB flash)

Configs are in `platformio.ini`. Keep them in sync when adding features.

## Current Usage Pattern

```cpp
void loop() {
    webSocket.loop();  // Always process WebSocket events

    WiFiClient client = server.accept();
    if (!client) return;

    client.setNoDelay(true);

    HttpRequest req = HttpRequestReader::read(client);
    if (!req) {  // operator bool() returns isValid()
        return;
    }

    Serial.println(req.toString());  // "GET /ping" or "POST /echo [15 bytes]"

    // Route handling with pattern matching
    HttpResponse res;
    if (req.matchGet("/")) {
        res = HttpResponse::html(index_htm, index_htm_len);
    } else if (req.matchGet("/ping")) {
        res = HttpResponse::text("pong");
    } else if (req.matchPost("/echo")) {
        res = HttpResponse::json(req.body());
    } else if (req.matchGet("/api/user/{id}")) {
        String userId = req.pathParam("id");  // Extract path parameter
        res = HttpResponse::json("{\"id\":\"" + userId + "\"}");
    } else {
        res = HttpResponse::notFound();
    }

    // Query parameters: /search?q=hello&limit=10
    String query = req.queryParam("q");
    bool hasLimit = req.hasQueryParam("limit");

    HttpResponseWriter::write(client, res);
    client.stop();
}
```

### HttpRequest Route Matching

```cpp
// Pattern matching with path parameters
req.match("GET", "/api/user/{id}")      // Returns true if matches
req.matchGet("/api/user/{id}")          // Shorthand for GET
req.matchPost("/api/data")              // Shorthand for POST
req.matchPut("/api/user/{id}")          // Shorthand for PUT
req.matchDelete("/api/user/{id}")       // Shorthand for DELETE

// Extract matched path parameters
String id = req.pathParam("id");

// Query parameters (?key=value)
String q = req.queryParam("q");
bool has = req.hasQueryParam("limit");
```

## TODO

1. Add API routes (shader CRUD, animation control)
2. Re-integrate animation system (Anime)
3. Re-integrate BLE device manager
4. Clean up commented AsyncWebServer code once stable
5. (Optional) Implement AsyncHttpReader for non-blocking reads if WebSocket latency becomes an issue
