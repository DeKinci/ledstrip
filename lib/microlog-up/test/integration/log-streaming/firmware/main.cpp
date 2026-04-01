/**
 * Test firmware for microlog-up log streaming suite.
 *
 * Sets up MicroLogProto (log streaming via MicroProto properties) and
 * exposes HTTP endpoints to trigger log messages at different levels.
 *
 * Properties (registered by MicroLogProto):
 *   - sys/errorLog  (STREAM, readonly, persistent — WARN/ERROR entries)
 *   - sys/logStream (STREAM, readonly, hidden — all log entries)
 *
 * HTTP endpoints:
 *   - GET /debug/log?level=info&tag=Test&msg=hello  — emit a log entry
 *   - GET /debug/bootcount                          — return boot count
 *   - GET /ping                                     — health check
 *
 * Build: pio run -e test_logup
 */

#include <Arduino.h>
#include <WiFi.h>

#include <PropertySystem.h>
#include <MicroProtoController.h>
#include <MicroProtoServer.h>
#include <MicroLog.h>
#include <MicroLogProto.h>

#include "wifiman/WiFiMan.h"
#include "webutils/HttpDispatcher.h"

// Networking
static WiFiServer httpServer(80);
static HttpDispatcher httpDispatcher;
static MicroProto::MicroProtoController controller;
static MicroProto::MicroProtoServer wsServer(81);

void setup() {
    Serial.begin(115200);
    Serial.println("test_logup: starting");

    WiFiMan::begin();

    // Initialize log streaming BEFORE controller
    MicroLog::MicroLogProto::init();

    httpServer.begin();

    httpDispatcher.onGet("/ping", [](HttpRequest& req) {
        return HttpResponse::text("pong");
    });

    httpDispatcher.onGet("/debug/log", [](HttpRequest& req) {
        String level = req.queryParam("level");
        String tag = req.queryParam("tag");
        String msg = req.queryParam("msg");

        if (tag.length() == 0) tag = "Test";
        if (msg.length() == 0) msg = "test message";

        if (level == "error") {
            LOG_ERROR(tag.c_str(), "%s", msg.c_str());
        } else if (level == "warn") {
            LOG_WARN(tag.c_str(), "%s", msg.c_str());
        } else {
            LOG_INFO(tag.c_str(), "%s", msg.c_str());
        }

        return HttpResponse::json("{\"ok\":true}");
    });

    httpDispatcher.onGet("/debug/bootcount", [](HttpRequest& req) {
        char buf[32];
        snprintf(buf, sizeof(buf), "{\"bootCount\":%d}",
            MicroLog::MicroLogProto::instance().bootCount());
        return HttpResponse::json(buf);
    });

    controller.begin();
    wsServer.begin(&controller);

    LOG_INFO("Startup", "test_logup firmware ready");
    Serial.println("test_logup: ready");
}

void loop() {
    wsServer.loop();
    controller.flushBroadcasts();

    WiFiClient client = httpServer.accept();
    if (client) {
        client.setNoDelay(true);
        HttpRequest req = HttpRequestReader::read(client);
        if (req) {
            HttpResponse res = httpDispatcher.dispatch(req);
            HttpResponseWriter::write(client, res);
        }
        client.stop();
    }
}
