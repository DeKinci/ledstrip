/**
 * Test firmware for microproto-gw suite.
 *
 * Runs GatewayClient connecting to a test gateway server.
 * The gateway URL is configured via build flag -DGATEWAY_URL.
 * Also exposes HTTP debug endpoints for test verification.
 *
 * Properties:
 *   - brightness (UINT8, 0-255)
 *   - enabled    (BOOL)
 *
 * Build: pio run -e test_mp_gw
 */

#include <Arduino.h>
#include <WiFi.h>

#include <PropertySystem.h>
#include <MicroProtoController.h>
#include <MicroProtoServer.h>
#include <GatewayClient.h>
#include <MicroLog.h>

#include "wifiman/WiFiMan.h"
#include "webutils/HttpDispatcher.h"

using namespace MicroProto;
using PB = PropertyBase;

// Properties
static Property<uint8_t> brightness("brightness", 128, PropertyLevel::LOCAL,
    Constraints<uint8_t>().min(0).max(255),
    "LED brightness",
    UIHints().setColor(UIColor::AMBER),
    PB::NOT_PERSISTENT);

static Property<bool> enabled("enabled", true, PropertyLevel::LOCAL,
    "Power switch",
    UIHints().setColor(UIColor::LIME),
    PB::NOT_PERSISTENT);

// Networking
static WiFiServer httpServer(80);
static HttpDispatcher httpDispatcher;
static MicroProtoController controller;
static MicroProtoServer wsServer(81);

void setup() {
    Serial.begin(115200);
    Serial.println("test_mp_gw: starting");

    WiFiMan::begin();

    httpServer.begin();
    httpDispatcher.onGet("/ping", [](HttpRequest& req) {
        return HttpResponse::text("pong");
    });

    httpDispatcher.onGet("/debug/gateway", [](HttpRequest& req) {
        char buf[128];
        snprintf(buf, sizeof(buf),
            "{\"connected\":%s,\"brightness\":%d,\"enabled\":%s}",
            GatewayClient::isConnected() ? "true" : "false",
            (int)brightness.get(),
            enabled.get() ? "true" : "false");
        return HttpResponse::json(buf);
    });

    // MicroProto over WebSocket (local) + Gateway (remote)
    controller.begin();
    wsServer.begin(&controller);
    GatewayClient::init(&controller);

    Serial.println("test_mp_gw: ready");
}

void loop() {
    wsServer.loop();
    GatewayClient::loop();
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
