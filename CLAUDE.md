# LED Strip Project - Development Notes

## Project Overview

ESP32-based LED strip controller with web interface and WebSocket communication.

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

### Integration Test Suite

Auto-discovers ESP32 via ARP and runs HTTP/WebSocket tests. Located in `scripts/tests/`.

```
scripts/
├── integration_test.py      # Main runner
├── pio_integration.py       # PlatformIO hook
└── tests/
    ├── __init__.py          # Shared: Colors, TestResult, discover_esp32
    ├── test_http.py         # HTTP reliability + POST echo
    ├── test_websocket.py    # WebSocket reliability
    └── test_parallel.py     # Parallel HTTP + WS stress test
```

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
python scripts/integration_test.py

# Run with specific IP
python scripts/integration_test.py 192.168.1.100

# Run via PlatformIO
pio run -t test_device
```

#### What Each Test Does

| Test | Description |
|------|-------------|
| `test_http.py` | HTTP /ping reliability (15 requests) + POST /echo validation |
| `test_websocket.py` | WebSocket message round-trips (15 messages) |
| `test_parallel.py` | HTTP + WebSocket simultaneously for 5 seconds |

#### Adding New Integration Tests

1. Create `scripts/tests/test_myfeature.py`:

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

2. Import and add to `scripts/integration_test.py`:

```python
from tests.test_myfeature import test_my_feature

def run_test_suite(ip: str) -> List[TestResult]:
    results = []
    # ... existing tests ...
    results.append(test_my_feature(ip))
    return results
```

### Native Unit Tests (run on laptop)

Tests for HttpRequest and other webutils that run locally without hardware:

```bash
# Run all native tests
pio test -e native

# Run with verbose output
pio test -e native -v
```

Tests are in `test/test_native_http/`. Uses mock Arduino.h from `test/native_mocks/Arduino/`.

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
