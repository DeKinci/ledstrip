// Test firmware for MicroBLE integration tests.
// Runs BLE GATT echo service via BleMessageService + HTTP debug API.
//
// BLE service: echo protocol — messages written to RX are echoed on TX.
// Large messages are automatically fragmented/reassembled by BleMessageService.
//
// HTTP debug API exposes BLE state for test verification:
//   GET /ping                 -> "pong"
//   GET /debug/ble/state      -> JSON: connected count, client slots, MTUs
//   GET /debug/ble/stats      -> JSON: messages received/sent, bytes, max size seen
//   POST /debug/ble/reset     -> reset stats counters

#include <Arduino.h>
#include <WiFi.h>
#include <HttpServer.h>
#include <WiFiMan.h>
#include <MicroLog.h>
#include <ResponseBuffer.h>
#include <MicroBLE.h>
#include <BleMessageService.h>

static const char* TAG = "TestBLE";

// Test service UUIDs
#define TEST_SERVICE_UUID  "aaa10001-f5a3-4aa0-b726-5d1be14a1d00"
#define TEST_RX_CHAR_UUID  "aaa10002-f5a3-4aa0-b726-5d1be14a1d00"
#define TEST_TX_CHAR_UUID  "aaa10003-f5a3-4aa0-b726-5d1be14a1d00"

static constexpr uint8_t MAX_TEST_CLIENTS = 4;
static constexpr size_t MAX_MSG_SIZE = 2048;

// Stats
static uint32_t msgRecvCount = 0;
static uint32_t msgSentCount = 0;
static uint32_t bytesReceived = 0;
static uint32_t bytesSent = 0;
static size_t maxMsgSize = 0;
static uint32_t connectCount = 0;
static uint32_t disconnectCount = 0;

// Message service
static MicroBLE::BleMessageService<MAX_MSG_SIZE, MAX_TEST_CLIENTS> msgService;

// Echo handler
class EchoHandler : public MicroBLE::MessageHandler {
public:
    void onMessage(uint8_t slot, const uint8_t* data, size_t len) override {
        msgRecvCount++;
        bytesReceived += len;
        if (len > maxMsgSize) maxMsgSize = len;

        // Echo back
        msgService.sendMessage(slot, data, len);
        msgSentCount++;
        bytesSent += len;
    }

    void onConnect(uint8_t slot) override {
        connectCount++;
        LOG_INFO(TAG, "Client connected: slot %u", slot);

        if (msgService.connectedCount() < MAX_TEST_CLIENTS) {
            MicroBLE::startAdvertising();
        }
    }

    void onDisconnect(uint8_t slot) override {
        disconnectCount++;
        LOG_INFO(TAG, "Client disconnected: slot %u", slot);
        MicroBLE::startAdvertising();
    }
};

static EchoHandler echoHandler;

// HTTP
static HttpServer http(80);
static WiFiMan::WiFiManager wifiManager(&http.dispatcher());

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
    Serial.println("\n\n=== MicroBLE Integration Test ===");

    MicroLog::Logger::init();

    // Initialize BLE
    MicroBLE::init("TestBLE");

    MicroBLE::GattConfig config = {};
    config.serviceUUID = TEST_SERVICE_UUID;
    config.rxUUID = TEST_RX_CHAR_UUID;
    config.txUUID = TEST_TX_CHAR_UUID;
    config.txIndicate = false;
    config.maxClients = MAX_TEST_CLIENTS;

    msgService.begin(&echoHandler, config);

    // Advertise
    MicroBLE::advertising()->addServiceUUID(TEST_SERVICE_UUID);
    MicroBLE::advertising()->enableScanResponse(true);
    MicroBLE::startAdvertising();

    LOG_INFO(TAG, "BLE echo service started: %s", TEST_SERVICE_UUID);

    // HTTP debug endpoints
    http.dispatcher().onGet("/debug/ble/state", [](HttpRequest& req, ResponseBuffer& buf) {
        char clients[256] = "[";
        size_t pos = 1;
        for (uint8_t i = 0; i < MAX_TEST_CLIENTS; i++) {
            if (i > 0) pos += snprintf(clients + pos, sizeof(clients) - pos, ",");
            pos += snprintf(clients + pos, sizeof(clients) - pos,
                "{\"slot\":%u,\"connected\":%s,\"mtu\":%u}",
                i,
                msgService.isConnected(i) ? "true" : "false",
                msgService.mtu(i));
        }
        snprintf(clients + pos, sizeof(clients) - pos, "]");

        return jsonDebug(buf,
            "{\"connectedCount\":%u,\"clients\":%s}",
            msgService.connectedCount(), clients);
    });

    http.dispatcher().onGet("/debug/ble/stats", [](HttpRequest& req, ResponseBuffer& buf) {
        return jsonDebug(buf,
            "{\"messagesReceived\":%lu,\"messagesSent\":%lu,"
            "\"bytesReceived\":%lu,\"bytesSent\":%lu,"
            "\"maxMessageSize\":%zu,"
            "\"connects\":%lu,\"disconnects\":%lu}",
            (unsigned long)msgRecvCount, (unsigned long)msgSentCount,
            (unsigned long)bytesReceived, (unsigned long)bytesSent,
            maxMsgSize,
            (unsigned long)connectCount, (unsigned long)disconnectCount);
    });

    http.dispatcher().onGet("/debug/ble/heap", [](HttpRequest& req, ResponseBuffer& buf) {
        return jsonDebug(buf, "{\"freeHeap\":%lu}", (unsigned long)ESP.getFreeHeap());
    });

    http.dispatcher().onPost("/debug/ble/reset", [](HttpRequest& req, ResponseBuffer&) {
        msgRecvCount = 0;
        msgSentCount = 0;
        bytesReceived = 0;
        bytesSent = 0;
        maxMsgSize = 0;
        connectCount = 0;
        disconnectCount = 0;
        return HttpResponse::json("{\"success\":true}");
    });

    wifiManager.begin();
    http.begin();
    LOG_INFO(TAG, "HTTP debug server started on port 80");
}

void loop() {
    wifiManager.loop();
    http.loop();
    msgService.loop();
    MicroLog::Logger::loop();
}
