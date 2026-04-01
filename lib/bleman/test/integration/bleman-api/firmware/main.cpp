// Test firmware for BleMan integration tests.
// Runs BleManager with BleButtonDriver + HTTP debug API.
//
// The PC acts as a BLE HID peripheral via `bless`.
// ESP32 discovers it, connects, and receives button notifications.
//
// Debug API:
//   GET  /ping                      -> "pong" (from HttpServer)
//   GET  /debug/bleman/state        -> JSON: known/connected counts, scan state
//   GET  /debug/bleman/actions      -> JSON: list of actions received by button driver
//   POST /debug/bleman/reset        -> clear action log
//   GET  /debug/bleman/heap         -> free heap

#include <Arduino.h>
#include <WiFi.h>
#include <HttpServer.h>
#include <WiFiMan.h>
#include <MicroLog.h>
#include <ResponseBuffer.h>
#include <MicroBLE.h>
#include <BleMan.h>
#include <BleButtonDriver.h>
#include <GestureDetector.h>

static const char* TAG = "TestBleMan";

// Custom service/char UUIDs for testing (avoids CoreBluetooth blocking HID 0x1812)
#define TEST_BTN_SERVICE_UUID "bbb10001-f5a3-4aa0-b726-5d1be14a1d00"
#define TEST_BTN_REPORT_UUID  "bbb10002-f5a3-4aa0-b726-5d1be14a1d00"

// Action log — ring buffer of recent actions for test verification
static constexpr size_t MAX_ACTION_LOG = 64;
static SequenceDetector::Action actionLog[MAX_ACTION_LOG];
static size_t actionLogCount = 0;
static size_t actionLogTotal = 0;

static void logAction(SequenceDetector::Action action) {
    if (actionLogCount < MAX_ACTION_LOG) {
        actionLog[actionLogCount++] = action;
    }
    actionLogTotal++;
    LOG_INFO(TAG, "Action: %d (total: %zu)", (int)action, actionLogTotal);
}

static const char* actionName(SequenceDetector::Action a) {
    switch (a) {
        case SequenceDetector::Action::SINGLE_CLICK:    return "single_click";
        case SequenceDetector::Action::DOUBLE_CLICK:    return "double_click";
        case SequenceDetector::Action::HOLD_TICK:       return "hold_tick";
        case SequenceDetector::Action::CLICK_HOLD_TICK: return "click_hold_tick";
        case SequenceDetector::Action::HOLD_END:        return "hold_end";
        default: return "none";
    }
}

// BLE setup — register button driver with custom test UUID + action logging
static BleDriver* buttonFactory(const BleKnownDevice& dev) {
    auto* btn = BleButtonDriver::allocate();
    if (!btn) return nullptr;
    btn->setServiceUUID(TEST_BTN_SERVICE_UUID);
    btn->setActionCallback(logAction);
    return btn;
}

static HttpServer http(80);
static WiFiMan::WiFiManager wifiManager(&http.dispatcher());
static BleMan::BleManager bleManager(&http.dispatcher());

static HttpResponse jsonDebug(ResponseBuffer& buf, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char* start = buf.writePtr();
    int len = vsnprintf(start, buf.remaining(), fmt, args);
    va_end(args);
    if (len > 0) buf.advance(len);
    return HttpResponse::json(start, (size_t)(len > 0 ? len : 0), 200);
}

void setup() {
    Serial.begin(115200);
    delay(600);
    Serial.println("\n\n=== BleMan Integration Test ===");

    MicroLog::Logger::init();

    // Initialize BLE stack
    MicroBLE::init("TestBleMan");

    // Register button driver and start manager
    bleManager.registerDriver("button", buttonFactory);
    bleManager.begin();

    LOG_INFO(TAG, "BleManager started");

    // Debug endpoints
    http.dispatcher().onGet("/debug/bleman/state", [](HttpRequest& req, ResponseBuffer& buf) {
        return jsonDebug(buf,
            "{\"knownCount\":%zu,\"connectedCount\":%zu,"
            "\"scanning\":%s,\"driverTypes\":%zu}",
            bleManager.getKnownDeviceCount(),
            bleManager.getConnectedDeviceCount(),
            bleManager.isScanning() ? "true" : "false",
            bleManager.getDriverTypeCount());
    });

    http.dispatcher().onGet("/debug/bleman/actions", [](HttpRequest& req, ResponseBuffer& buf) {
        char* start = buf.writePtr();
        size_t remaining = buf.remaining();
        size_t pos = 0;

        pos += snprintf(start + pos, remaining - pos,
            "{\"total\":%zu,\"actions\":[", actionLogTotal);

        for (size_t i = 0; i < actionLogCount && pos < remaining - 32; i++) {
            if (i > 0) pos += snprintf(start + pos, remaining - pos, ",");
            pos += snprintf(start + pos, remaining - pos,
                "\"%s\"", actionName(actionLog[i]));
        }

        pos += snprintf(start + pos, remaining - pos, "]}");
        buf.advance(pos);
        return HttpResponse::json(start, pos, 200);
    });

    http.dispatcher().onPost("/debug/bleman/reset", [](HttpRequest& req, ResponseBuffer&) {
        actionLogCount = 0;
        actionLogTotal = 0;
        return HttpResponse::json("{\"success\":true}");
    });

    http.dispatcher().onGet("/debug/bleman/heap", [](HttpRequest& req, ResponseBuffer& buf) {
        return jsonDebug(buf, "{\"freeHeap\":%lu}", (unsigned long)ESP.getFreeHeap());
    });

    // WiFi
    wifiManager.begin();
    http.begin();

    LOG_INFO(TAG, "Ready — HTTP on port 80, BLE active");
}

void loop() {
    wifiManager.loop();
    http.loop();
    bleManager.loop();
    MicroLog::Logger::loop();
}
