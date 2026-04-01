/**
 * Test firmware for microproto-ble suite.
 *
 * Registers properties with mixed ble_exposed flags to test BLE transport
 * filtering, multiple types, and bidirectional sync over BLE.
 *
 * ble_exposed=true:  brightness, enabled, counter, label, rgb
 * ble_exposed=false: speed, hiddenVal
 *
 * Build: pio run -e test_mp_ble
 */

#include <Arduino.h>
#include <WiFi.h>

#include <PropertySystem.h>
#include <MicroProtoController.h>
#include <MicroProtoBleServer.h>
#include <MicroBLE.h>
#include <MicroLog.h>

#include "wifiman/WiFiMan.h"
#include "webutils/HttpDispatcher.h"

using namespace MicroProto;
using PB = PropertyBase;

// ble_exposed properties — visible over BLE
static Property<uint8_t> brightness("brightness", 128, PropertyLevel::LOCAL,
    Constraints<uint8_t>().min(0).max(255).step(1),
    "LED brightness",
    UIHints().setColor(UIColor::AMBER).setWidget(Widget::Decimal::SLIDER),
    PB::NOT_PERSISTENT, PB::NOT_READONLY, PB::NOT_HIDDEN, true /* ble_exposed */);

static Property<bool> enabled("enabled", true, PropertyLevel::LOCAL,
    "Power switch",
    UIHints().setColor(UIColor::LIME).setWidget(Widget::Bool::TOGGLE),
    PB::NOT_PERSISTENT, PB::NOT_READONLY, PB::NOT_HIDDEN, true /* ble_exposed */);

static Property<int32_t> counter("counter", 0, PropertyLevel::LOCAL,
    Constraints<int32_t>().min(-1000000).max(1000000),
    "Counter value",
    UIHints(),
    PB::NOT_PERSISTENT, PB::NOT_READONLY, PB::NOT_HIDDEN, true /* ble_exposed */);

static ArrayProperty<uint8_t, 3> rgb("rgb", {255, 0, 0}, PropertyLevel::LOCAL,
    ArrayConstraints<uint8_t>().min(0).max(255),
    "RGB color",
    UIHints().setColor(UIColor::ROSE),
    PB::NOT_PERSISTENT, PB::NOT_READONLY, PB::NOT_HIDDEN, true /* ble_exposed */);

// ble_exposed list property
static Property<MicroList<uint8_t, 8, 32>> label("label", {0x48, 0x69},
    PropertyLevel::LOCAL,
    "Label bytes",
    UIHints(),
    PB::NOT_PERSISTENT, PB::NOT_READONLY, PB::NOT_HIDDEN, true /* ble_exposed */);

// NOT ble_exposed — should be filtered out over BLE transport
static Property<float> speed("speed", 1.0f, PropertyLevel::LOCAL,
    Constraints<float>().min(0.0f).max(10.0f).step(0.1f),
    "Animation speed",
    UIHints().setColor(UIColor::CYAN),
    PB::NOT_PERSISTENT, PB::NOT_READONLY, PB::NOT_HIDDEN, false /* NOT ble_exposed */);

static Property<int32_t> hiddenVal("hiddenVal", 42, PropertyLevel::LOCAL,
    "Hidden from BLE",
    UIHints(),
    PB::NOT_PERSISTENT, PB::NOT_READONLY, PB::NOT_HIDDEN, false /* NOT ble_exposed */);

// Networking
static WiFiServer httpServer(80);
static HttpDispatcher httpDispatcher;
static MicroProtoController controller;
static MicroProtoBleServer bleServer;

void setup() {
    Serial.begin(115200);
    Serial.println("test_mp_ble: starting");

    WiFiMan::begin();

    // HTTP debug endpoints
    httpServer.begin();
    httpDispatcher.onGet("/ping", [](HttpRequest& req) {
        return HttpResponse::text("pong");
    });

    httpDispatcher.onGet("/debug/properties", [](HttpRequest& req) {
        char buf[384];
        snprintf(buf, sizeof(buf),
            "{\"count\":%d,\"bleExposedCount\":5,\"brightness\":%d,\"enabled\":%s,"
            "\"counter\":%ld,\"speed\":%.1f,\"hiddenVal\":%ld}",
            (int)PropertyBase::count,
            (int)brightness.get(),
            enabled.get() ? "true" : "false",
            (long)counter.get(),
            speed.get(),
            (long)hiddenVal.get());
        return HttpResponse::json(buf);
    });

    // HTTP endpoint to trigger property changes (for testing BLE broadcasts)
    httpDispatcher.onGet("/debug/set-brightness", [](HttpRequest& req) {
        String val = req.queryParam("v");
        if (val.length() > 0) {
            brightness.set(val.toInt());
        }
        char buf[32];
        snprintf(buf, sizeof(buf), "{\"brightness\":%d}", (int)brightness.get());
        return HttpResponse::json(buf);
    });

    httpDispatcher.onGet("/debug/ble/clients", [](HttpRequest& req) {
        char buf[64];
        snprintf(buf, sizeof(buf), "{\"connected\":%d}", bleServer.connectedClients());
        return HttpResponse::json(buf);
    });

    // MicroProto over BLE
    MicroBLE::init("MPBleTest");
    controller.begin();
    bleServer.begin(&controller);

    Serial.println("test_mp_ble: ready");
}

void loop() {
    bleServer.loop();
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
