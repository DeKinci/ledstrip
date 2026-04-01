// LED Strip Controller - HTTP + MicroProto + Anime
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <HttpServer.h>
#include <WiFiMan.h>
#include <PropertySystem.h>
#include <MicroProtoController.h>
#include <MicroProtoServer.h>
#include <MicroProtoBleServer.h>

#include "animations/Anime.h"
#include "web/LedApiController.h"
#ifdef ENCODER_ENABLED
#include "input/EncoderInput.hpp"
#endif
#ifdef MIC_ENABLED
#include "input/MicInput.hpp"
#endif
#include <GatewayClient.h>
#include "ble_setup.h"
#include <MicroBLE.h>
#include <BleMan.h>
#include <MicroLog.h>
#include <MicroLogProto.h>

#include "gen/w_index_htm.h"
#include "gen/w_proto_htm.h"
#include "gen/w_microproto_client_js.h"
#include "gen/w_microproto_ui_js.h"
#ifdef MIC_ENABLED
#include "gen/w_mic_htm.h"
#endif

HttpServer http(80);
MicroProto::MicroProtoController protoController;
MicroProto::MicroProtoServer protoServer(81);
MicroProto::MicroProtoBleServer protoBle;
WiFiMan::WiFiManager wifiManager(&httpDispatcher);
BleMan::BleManager bleManager(&httpDispatcher);

CallResult<void*> animeStatus(nullptr);

// Metrics — auto-registered, printed every 10s
#include <Gauge.h>
static MicroLog::Gauge<uint32_t> mHeap("heap", "B", 5000);
static MicroLog::Gauge<int32_t> mRSSI("rssi", "dBm", 5000);
static MicroLog::Gauge<uint8_t> mWSClients("ws_clients", nullptr, 2000);
static MicroLog::Gauge<uint8_t> mBLEClients("ble_clients", nullptr, 2000);

void setup() {
    Serial.begin(115200);
    delay(600);  // crucial for wifi
    Serial.println("\n\n=== LED Strip Controller ===");
    Serial.printf("1. Boot: Free heap: %lu bytes\n", ESP.getFreeHeap());

    // Initialize BLE FIRST (before WiFi) - they share the radio
    MicroBLE::init("SmartGarland");

    // Register BLE drivers and start manager
    BleSetup::init(bleManager);
    bleManager.begin();

    // Initialize MicroProto controller + transports
    protoController.begin();
    protoBle.begin(&protoController);
    Serial.printf("2. After BLE init: Free heap: %lu bytes\n", ESP.getFreeHeap());

    // Initialize logging (boot counter, persistent error log)
    MicroLog::MicroLogProto::init();
    MicroLog::Logger::setMetricsInterval(10000);

    // Initialize property system (includes loading persistent values)
    MicroProto::PropertySystem::init();
    Serial.printf("2. After PropertySystem: Free heap: %lu bytes, Properties: %d\n",
                  ESP.getFreeHeap(), MicroProto::PropertySystem::getPropertyCount());

#ifdef ENCODER_ENABLED
    EncoderInput::init();
    Serial.printf("3. After EncoderInput: Free heap: %lu bytes\n", ESP.getFreeHeap());
#endif

#ifdef MIC_ENABLED
    MicInput::init();
    Serial.printf("3b. After MicInput: Free heap: %lu bytes\n", ESP.getFreeHeap());
#endif

    // Initialize Anime (LED animation system)
    animeStatus = Anime::connect();
    while (animeStatus.hasError()) {
        Serial.printf("Error starting Anime: %s\n", animeStatus.getMessage().c_str());
        delay(1000);
        animeStatus = Anime::connect();
    }
    Serial.printf("5. After Anime: Free heap: %lu bytes\n", ESP.getFreeHeap());

    // Configure WiFiManager
    wifiManager.setAPCredentials("LED-Setup", "");  // Open AP for setup
    wifiManager.setHostname("ledstrip");
    wifiManager.credentials().addNetwork("Citadel", "kekovino4ka", 100);  // Default network

    // Static resources (ETag = firmware hash, auto-invalidated on reflash)
    httpDispatcher.resource("/", index_htm);
    httpDispatcher.resource("/proto", proto_htm);
#ifdef MIC_ENABLED
    httpDispatcher.resource("/mic", mic_htm);
#endif
    httpDispatcher.resource("/js/proto.js", microproto_client_js);
    httpDispatcher.resource("/js/ui.js", microproto_ui_js);

    // Dynamic routes — no ETag
    httpDispatcher.onPost("/echo", [](HttpRequest& req, ResponseBuffer&) {
        return HttpResponse::json(req.body().data(), req.body().length());
    });

    // Register LED API routes (shader CRUD, animation control)
    LedApiController::registerRoutes(httpDispatcher);

    // Start WiFiManager (registers /wifiman routes, handles WiFi connection)
    wifiManager.begin();

    // Callback when connected
    wifiManager.onConnected([](const String& ssid) {
        Serial.printf("Connected to %s\n", ssid.c_str());
        WiFi.setSleep(false);  // Low latency mode

        // Start mDNS for led.local
        if (MDNS.begin("led")) {
            Serial.println("mDNS: led.local");
        }

        // Connect to gateway if configured
        GatewayClient::init(&protoController);
    });

    // Start HTTP server (works in both AP and STA mode)
    http.begin();
    Serial.println("HTTP server on port 80");

    // Start MicroProto binary WebSocket server
    protoServer.begin(&protoController);
    Serial.println("MicroProto server on port 81");

#ifdef MIC_ENABLED
    // Start mic debug audio WebSocket server
    MicInput::initDebugStream();
#endif

    Serial.printf("6. Setup complete: Free heap: %lu bytes, Routes: %d\n",
                  ESP.getFreeHeap(), httpDispatcher.routeCount());
    Serial.println("=== Ready ===");
}

void loop() {
    // Network services
    wifiManager.loop();
    protoServer.loop();
    protoBle.loop();
    GatewayClient::loop();
    http.loop();

    // Property system (handles dirty tracking and persistence)
    MicroProto::PropertySystem::loop();
    protoController.flushBroadcasts();
    yield();

    // Physical input
#ifdef ENCODER_ENABLED
    EncoderInput::loop();
#endif
#ifdef MIC_ENABLED
    MicInput::loop();
    MicInput::loopDebugStream();
#endif
    yield();

    // BLE device management
    bleManager.loop();
    yield();

    // LED animation
    animeStatus = Anime::draw();
    yield();

    // Update metrics (printed automatically by Logger::loop)
    mHeap.set(ESP.getFreeHeap());
    mRSSI.set(WiFi.RSSI());
    mWSClients.set(protoServer.connectedClients());
    mBLEClients.set(protoBle.connectedClients());
    MicroLog::Logger::loop();
}
