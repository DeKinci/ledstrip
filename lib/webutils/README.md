# webutils

A minimal, zero-copy HTTP toolkit for ESP32. Not a web framework — a set of composable building blocks that other libraries plug into.

## Role

webutils owns **HTTP as infrastructure**. It exists so that:

- Any library can register routes on a shared dispatcher without owning the server
- Route handling is zero-copy — parsing produces `StringView` references into the request buffer, not `String` allocations
- The application controls the server lifecycle; libraries only register handlers
- Static resources are served compressed with automatic ETag/304 support tied to the firmware build hash

The key abstraction is `HttpDispatcher` — a shared route table that multiple libraries contribute to (wifiman adds `/wifiman/*`, bleman adds `/bleman/*`, the app adds its own routes). `HttpServer` is just plumbing that feeds requests into the dispatcher.

## What It Is Not

- Not async — reads are blocking with timeouts, one request at a time. This is intentional for embedded: simple, predictable, no framework overhead.
- Not a framework — no middleware, no sessions, no templating. Just parse, route, respond.
- Not coupled to WiFi — works with any `WiFiClient`. The server is optional; you can use the parser and dispatcher standalone.

## HttpServer

Wraps `WiFiServer` + `HttpDispatcher` + buffers. Call `loop()` from your main loop.

```cpp
HttpServer server(80);

void setup() {
    auto& d = server.dispatcher();
    d.onGet("/ping", [](HttpRequest& req, ResponseBuffer& buf) {
        return HttpResponse::text("pong");
    });
    server.begin();
}

void loop() {
    server.loop();
}
```

### Reader Tuning

```cpp
server.config().reader.firstByteTimeoutMs = 5000;  // wait for client to start sending
server.config().reader.readTimeoutMs = 500;         // timeout after first byte arrives
server.config().reader.maxBodySize = 8192;
```

## HttpDispatcher — the shared route table

This is the core abstraction. Libraries receive a `HttpDispatcher&` and register their routes on it. The application owns the dispatcher (usually via `HttpServer`).

```cpp
// Any library can do this given a dispatcher reference:
void setupRoutes(HttpDispatcher& d) {
    d.onGet("/mylib/status", handler);
    d.onPost("/mylib/config", handler);
}
```

### Route features

```cpp
// Path parameters
d.onGet("/api/user/{id}", [](HttpRequest& req, ResponseBuffer& buf) {
    StringView id = req.pathParam("id");
    return HttpResponse::json(buf.printf("{\"id\":\"%.*s\"}", (int)id.length(), id.data()), buf);
});

// Priority (higher = matched first, useful for captive portal overrides)
d.onGet("/", portalHandler, /*priority=*/100);

// ETag (auto 304 Not Modified)
d.onGet("/config", handler, "v1.2.3");

// Void handlers (auto-wrapped in HttpResponse::ok())
d.onPost("/api/reboot", [](HttpRequest& req, ResponseBuffer& buf) {
    scheduleReboot();
});

// Static compressed resources with firmware-hash ETag
d.resource("/", indexResource);

// Custom 404
d.onNotFound(handler);
```

### Handler signature

```cpp
using Handler = MicroFunction<HttpResponse(HttpRequest&, ResponseBuffer&), 8>;
```

Receives the parsed request and a scratch buffer for building responses. Returns `HttpResponse`.

## HttpRequest — zero-copy parsing

All getters return `StringView` into the original buffer. No allocations.

```cpp
req.method();   // "GET"
req.path();     // "/api/user/42"
req.body();     // request body

req.header("Content-Type");
req.hasHeader("Authorization");

req.matchGet("/api/user/{id}");
req.pathParam("id");

req.queryParam("q");        // /search?q=hello
req.hasQueryParam("limit");

if (!req) { /* invalid */ }
```

Up to 4 path params and 4 query params.

## HttpResponse

```cpp
HttpResponse::text("pong");
HttpResponse::json("{\"ok\":true}");
HttpResponse::html(data, len);
HttpResponse::ok();
HttpResponse::notFound();
HttpResponse::badRequest("missing field");
HttpResponse::withStatus(429, "rate limited");
HttpResponse::brotli(data, len, "text/html");
```

## ResponseBuffer

Scratch space for dynamic responses inside handlers:

```cpp
d.onGet("/status", [](HttpRequest& req, ResponseBuffer& buf) {
    buf.printf("{\"heap\":%u}", ESP.getFreeHeap());
    return HttpResponse::json(buf.data(), buf.length());
});
```

## Buffer Sizes

| Buffer | Default | Override |
|--------|---------|----------|
| Request | 4096 | `#define HTTP_REQUEST_BUFFER_SIZE` |
| Response | 4096 | `#define HTTP_RESPONSE_BUFFER_SIZE` |
