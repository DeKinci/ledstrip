/**
 * Test firmware for microproto-basic suite.
 *
 * Registers a small set of basic-type properties for integration testing:
 *   - brightness (UINT8, 0-255, default 128)
 *   - enabled (BOOL, default true)
 *   - speed (FLOAT32, 0.0-10.0, default 1.0)
 *   - count (INT32, default 0)
 *   - label (LIST<UINT8>, for string-like data)
 *
 * Build: pio run -e test_mp_basic
 */

#include <Arduino.h>
#include <WiFi.h>

#include "microproto/PropertySystem.h"
#include "microproto/MicroProtoController.h"
#include "microproto/transport/MicroProtoServer.h"
#include "wifiman/WiFiMan.h"
#include "webutils/HttpDispatcher.h"

// Properties
static auto& brightness = PropertySystem::create<uint8_t>("brightness", 128)
    .min(0).max(255).step(1)
    .color(UIColor::AMBER).widget(WidgetHint::SLIDER)
    .done();

static auto& enabled = PropertySystem::create<bool>("enabled", true)
    .color(UIColor::LIME).widget(WidgetHint::TOGGLE)
    .done();

static auto& speed = PropertySystem::create<float>("speed", 1.0f)
    .min(0.0f).max(10.0f).step(0.1f)
    .color(UIColor::CYAN)
    .done();

static auto& count = PropertySystem::create<int32_t>("count", 0)
    .done();

// Networking
static WiFiServer httpServer(80);
static HttpDispatcher httpDispatcher;
static MicroProtoServer wsServer(81);
static MicroProtoController controller;

void setup() {
    Serial.begin(115200);
    Serial.println("test_mp_basic: starting");

    WiFiMan::begin();

    httpServer.begin();
    httpDispatcher.onGet("/ping", [](HttpRequest& req) {
        return HttpResponse::text("pong");
    });

    controller.addTransport(wsServer);
    controller.begin();

    Serial.println("test_mp_basic: ready");
}

void loop() {
    wsServer.loop();
    controller.loop();

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
