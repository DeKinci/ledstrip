/**
 * Test firmware for webutils http-basic integration suite.
 * Build: pio run -e test_http_basic
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <HttpServer.h>
#include <WiFiMan.h>

// Helper: serialize ArduinoJson doc into ResponseBuffer
static HttpResponse jsonResponse(const JsonDocument& doc, ResponseBuffer& buf, int code = 200) {
    char* start = buf.writePtr();
    size_t len = serializeJson(doc, start, buf.remaining());
    buf.advance(len);
    return HttpResponse::json(start, len, code);
}

static HttpServer http(80);
static WiFiMan::WiFiManager wifiManager;

void setup() {
    Serial.begin(115200);
    Serial.println("test_http_basic: starting");

    wifiManager.begin();

    http.dispatcher().onGet("/ping", [](HttpRequest& req, ResponseBuffer&) {
        return HttpResponse::text("pong");
    });

    http.dispatcher().onGet("/json", [](HttpRequest& req, ResponseBuffer&) {
        return HttpResponse::json("{\"status\":\"ok\"}");
    });

    http.dispatcher().onPost("/echo", [](HttpRequest& req, ResponseBuffer&) {
        auto ct = req.header("Content-Type");
        return HttpResponse()
            .status(200)
            .contentType(ct.length() > 0 ? "text/plain" : "text/plain")  // Content-type from request not easily stored as const char*
            .body(req.body().data(), req.body().length());
    });

    http.dispatcher().onGet("/status/201", [](HttpRequest& req, ResponseBuffer&) {
        return HttpResponse::withStatus(201, "Created");
    });

    http.dispatcher().onGet("/status/404", [](HttpRequest& req, ResponseBuffer&) {
        return HttpResponse::notFound("custom not found");
    });

    http.dispatcher().onGet("/headers", [](HttpRequest& req, ResponseBuffer& buf) {
        JsonDocument doc;
        auto host = req.header("Host");
        if (host.length() > 0) doc["host"] = String(host.data(), host.length());
        auto ua = req.header("User-Agent");
        if (ua.length() > 0) doc["user-agent"] = String(ua.data(), ua.length());
        auto custom = req.header("X-Test");
        if (custom.length() > 0) doc["x-test"] = String(custom.data(), custom.length());
        return jsonResponse(doc, buf);
    });

    http.dispatcher().onGet("/query", [](HttpRequest& req, ResponseBuffer& buf) {
        JsonDocument doc;
        auto a = req.queryParam("a");
        if (a.length() > 0) doc["a"] = String(a.data(), a.length());
        auto b = req.queryParam("b");
        if (b.length() > 0) doc["b"] = String(b.data(), b.length());
        return jsonResponse(doc, buf);
    });

    http.dispatcher().onGet("/user/{id}", [](HttpRequest& req, ResponseBuffer& buf) {
        auto id = req.pathParam("id");
        JsonDocument doc;
        doc["id"] = String(id.data(), id.length());
        return jsonResponse(doc, buf);
    });

    http.dispatcher().onGet("/slow", [](HttpRequest& req, ResponseBuffer&) {
        delay(500);
        return HttpResponse::text("done");
    });

    // All HTTP methods
    http.dispatcher().onPut("/method", [](HttpRequest& req, ResponseBuffer&) {
        return HttpResponse::text("put-ok");
    });
    http.dispatcher().onDelete("/method", [](HttpRequest& req, ResponseBuffer&) {
        return HttpResponse::text("delete-ok");
    });

    // Large response (built into ResponseBuffer)
    http.dispatcher().onGet("/large", [](HttpRequest& req, ResponseBuffer& buf) {
        char* start = buf.writePtr();
        for (int i = 0; i < 256 && buf.remaining() >= 16; i++) {
            buf.write("0123456789ABCDEF", 16);
        }
        size_t len = buf.writePtr() - start;
        return HttpResponse::text(start, len);
    });

    // Echo full request info as JSON
    http.dispatcher().onPost("/inspect", [](HttpRequest& req, ResponseBuffer& buf) {
        JsonDocument doc;
        doc["method"] = String(req.method().data(), req.method().length());
        doc["path"] = String(req.path().data(), req.path().length());
        doc["bodyLength"] = req.body().length();
        doc["body"] = String(req.body().data(), req.body().length());
        auto ct = req.header("Content-Type");
        if (ct.length() > 0) doc["contentType"] = String(ct.data(), ct.length());
        return jsonResponse(doc, buf);
    });

    // Nested path params
    http.dispatcher().onGet("/api/v1/user/{id}/item/{itemId}", [](HttpRequest& req, ResponseBuffer& buf) {
        JsonDocument doc;
        auto id = req.pathParam("id");
        auto itemId = req.pathParam("itemId");
        doc["userId"] = String(id.data(), id.length());
        doc["itemId"] = String(itemId.data(), itemId.length());
        return jsonResponse(doc, buf);
    });

    http.dispatcher().onGet("/html", [](HttpRequest& req, ResponseBuffer&) {
        return HttpResponse::html("<h1>Hello</h1>");
    });

    http.dispatcher().onPost("/void", [](HttpRequest& req, ResponseBuffer&) {
        return HttpResponse::withStatus(204);
    });

    // JSON POST that parses and echoes
    http.dispatcher().onPost("/json-echo", [](HttpRequest& req, ResponseBuffer& buf) {
        JsonDocument doc;
        if (!req.json(doc)) {
            return HttpResponse::badRequest("Invalid JSON");
        }
        doc["echoed"] = true;
        return jsonResponse(doc, buf);
    });

    http.dispatcher().onGet("/status/500", [](HttpRequest& req, ResponseBuffer&) {
        return HttpResponse::error("test error");
    });

    // ETag routes — etag registered at route level, checked before handler
    http.dispatcher().onGet("/etag/static", [](HttpRequest& req, ResponseBuffer&) {
        return HttpResponse::text("static content");
    }, "abc123");

    http.dispatcher().onGet("/etag/versioned", [](HttpRequest& req, ResponseBuffer&) {
        return HttpResponse::json("{\"v\":1}");
    }, "v1hash");

    http.begin();
    Serial.println("test_http_basic: ready");
}

void loop() {
    wifiManager.loop();
    http.loop();
}
