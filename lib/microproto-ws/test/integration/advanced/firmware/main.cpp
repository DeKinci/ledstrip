/**
 * Advanced test firmware for microproto-ws suite.
 *
 * Registers every property type, constraints, UI hints, RPC functions,
 * readonly/hidden flags — the full MicroProto feature set.
 *
 * Build: pio run -e test_mp_advanced
 */

#include <Arduino.h>
#include <WiFi.h>

#include <PropertySystem.h>
#include <MicroProtoController.h>
#include <MicroProtoServer.h>
#include <MicroLog.h>

#include "wifiman/WiFiMan.h"
#include "webutils/HttpDispatcher.h"

using namespace MicroProto;
using PB = PropertyBase;

// ============== Basic types ==============

static Property<bool> propBool("test/bool", true, PropertyLevel::LOCAL,
    "Boolean test property",
    UIHints().setColor(UIColor::LIME).setWidget(Widget::Bool::TOGGLE));

static Property<int8_t> propInt8("test/int8", -42, PropertyLevel::LOCAL,
    Constraints<int8_t>().min(-100).max(100),
    "Signed 8-bit",
    UIHints().setColor(UIColor::CYAN));

static Property<uint8_t> propUint8("test/uint8", 128, PropertyLevel::LOCAL,
    Constraints<uint8_t>().min(0).max(255).step(1),
    "Unsigned 8-bit",
    UIHints().setColor(UIColor::AMBER).setWidget(Widget::Decimal::SLIDER).setUnit("%"));

static Property<int16_t> propInt16("test/int16", -1000, PropertyLevel::LOCAL,
    Constraints<int16_t>().min(-32000).max(32000),
    "Signed 16-bit");

static Property<uint16_t> propUint16("test/uint16", 5000, PropertyLevel::LOCAL,
    Constraints<uint16_t>().min(0).max(65535),
    "Unsigned 16-bit");

static Property<int32_t> propInt32("test/int32", -100000, PropertyLevel::LOCAL,
    Constraints<int32_t>().min(-1000000).max(1000000),
    "Signed 32-bit");

static Property<float> propFloat("test/float", 3.14f, PropertyLevel::LOCAL,
    Constraints<float>().min(-100.0f).max(100.0f).step(0.01f),
    "32-bit float",
    UIHints().setColor(UIColor::SKY));

// ============== Readonly property ==============

static Property<uint8_t> propReadonly("test/readonly", 42, PropertyLevel::LOCAL,
    "Readonly value",
    UIHints().setColor(UIColor::SLATE),
    PB::NOT_PERSISTENT, PB::READONLY);

// ============== Hidden property ==============

static Property<int32_t> propHidden("test/hidden", 999, PropertyLevel::LOCAL,
    "Hidden internal value",
    UIHints(),
    PB::NOT_PERSISTENT, PB::NOT_READONLY, PB::HIDDEN);

// ============== Array properties ==============

static ArrayProperty<uint8_t, 3> propRgb("test/rgb", {255, 128, 0}, PropertyLevel::LOCAL,
    ArrayConstraints<uint8_t>().min(0).max(255),
    "RGB color",
    UIHints().setColor(UIColor::ROSE).setWidget(Widget::Color::PICKER));

static ArrayProperty<int32_t, 4> propIntArray("test/intArray", {10, -20, 30, -40},
    PropertyLevel::LOCAL,
    "Int32 array");

// ============== List properties ==============

static Property<MicroList<uint8_t, 8, 64>> propByteList("test/byteList",
    {0x48, 0x65, 0x6C, 0x6C, 0x6F},  // "Hello"
    PropertyLevel::LOCAL,
    "Byte list");

static Property<MicroList<int32_t, 4, 16>> propIntList("test/intList",
    {100, -200, 300},
    PropertyLevel::LOCAL,
    "Int32 list");

// ============== Object property ==============

struct TestPoint {
    int32_t x;
    int32_t y;
    int32_t z;
};

MICROPROTO_FIELD_NAMES(TestPoint, "x", "y", "z");

static ObjectProperty<TestPoint> propObject("test/point",
    TestPoint{10, -20, 30},
    PropertyLevel::LOCAL,
    "3D point");

// ============== List of objects ==============

struct TestSegment {
    uint16_t start;
    uint16_t length;
    uint8_t flags;
    uint8_t _pad1;
    uint8_t _pad2;
    uint8_t _pad3;
};

MICROPROTO_FIELD_NAMES(TestSegment, "start", "length", "flags", "_pad1", "_pad2", "_pad3");

static Property<MicroList<TestSegment, 2, 8>> propSegments("test/segments",
    PropertyLevel::LOCAL,
    "Segment list");

// ============== Variant property ==============

static VariantProperty<3, 4> propVariant("test/variant",
    {
        VariantTypeDef{"ok", TYPE_UINT8, 1},
        VariantTypeDef{"error", TYPE_INT32, 4},
        VariantTypeDef{"flag", TYPE_BOOL, 1},
    },
    0, // initial type index
    {42, 0, 0, 0},  // initial data (uint8_t = 42)
    PropertyLevel::LOCAL,
    "Tagged union");

// ============== Stream property ==============

struct TestEvent {
    uint32_t timestamp;
    uint8_t code;
    uint8_t _pad1;
    uint8_t _pad2;
    uint8_t _pad3;
};

MICROPROTO_FIELD_NAMES(TestEvent, "timestamp", "code", "_pad1", "_pad2", "_pad3");

static StreamProperty<TestEvent, 8> propStream("test/events",
    PropertyLevel::LOCAL,
    "Event stream",
    UIHints().setWidget(Widget::Stream::LOG),
    PB::NOT_PERSISTENT, PB::READONLY);

// ============== RPC functions ==============

static const ParamDef addParams[] = {
    {"a", TYPE_INT32},
    {"b", TYPE_INT32}
};

static FunctionBase fnAdd("test/add", addParams, 2, TYPE_INT32, "Add two numbers");

static const ParamDef emitParams[] = {
    {"code", TYPE_UINT8}
};

static FunctionBase fnEmitEvent("test/emitEvent", emitParams, 1, TYPE_BOOL, "Emit a test event");

static FunctionBase fnNoParams("test/noParams", nullptr, 0, TYPE_BOOL, "No-param function");

// ============== Networking ==============

static WiFiServer httpServer(80);
static HttpDispatcher httpDispatcher;
static MicroProtoController controller;
static MicroProtoServer wsServer(81);

void setup() {
    Serial.begin(115200);
    Serial.println("test_mp_advanced: starting");

    WiFiMan::begin();

    httpServer.begin();
    httpDispatcher.onGet("/ping", [](HttpRequest& req) {
        return HttpResponse::text("pong");
    });

    httpDispatcher.onGet("/debug/state", [](HttpRequest& req) {
        char buf[256];
        snprintf(buf, sizeof(buf),
            "{\"properties\":%d,\"functions\":%d,\"wsClients\":%d}",
            (int)PropertyBase::count,
            (int)FunctionBase::count,
            (int)wsServer.connectedClients());
        return HttpResponse::json(buf);
    });

    // RPC handlers
    fnAdd.onCall([](ReadBuffer& params, WriteBuffer& result) -> bool {
        int32_t a, b;
        params.readFixed(&a, sizeof(a));
        params.readFixed(&b, sizeof(b));
        int32_t sum = a + b;
        result.writeFixed(&sum, sizeof(sum));
        return true;
    });

    fnEmitEvent.onCall([](ReadBuffer& params, WriteBuffer& result) -> bool {
        uint8_t code = params.readByte();
        TestEvent event{};
        event.timestamp = millis();
        event.code = code;
        propStream.push(event);
        uint8_t ok = 1;
        result.writeByte(ok);
        return true;
    });

    fnNoParams.onCall([](ReadBuffer& params, WriteBuffer& result) -> bool {
        uint8_t ok = 1;
        result.writeByte(ok);
        return true;
    });

    // Add initial segments
    TestSegment seg1 = {0, 10, 0x01, 0, 0, 0};
    TestSegment seg2 = {10, 20, 0x02, 0, 0, 0};
    propSegments.push_back(seg1);
    propSegments.push_back(seg2);

    controller.begin();
    wsServer.begin(&controller);

    Serial.println("test_mp_advanced: ready");
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
