// MINIMAL TEST - Direct routing like latency test
#include <Arduino.h>
#include <WiFi.h>
#include <HttpServer.h>
#include <WiFiMan.h>
#include <Property.h>
#include <ArrayProperty.h>
#include <ListProperty.h>
#include <PropertySystem.h>
#include <transport/MicroProtoServer.h>
#include "rsc/w_index_htm.h"
#include "rsc/w_proto_htm.h"
#include "rsc/w_microproto_client_js.h"

HttpServer http(80);
MicroProto::MicroProtoServer protoServer(81);
WiFiMan::WiFiManager wifiManager(&httpDispatcher);

// Basic properties with constraints and UI hints
MicroProto::Property<uint8_t> ledBrightness("ledBrightness", 128, MicroProto::PropertyLevel::LOCAL,
    MicroProto::Constraints<uint8_t>().min(0).max(255).step(1),
    "LED strip brightness level",
    MicroProto::UIHints().setColor(MicroProto::UIColor::AMBER).setIcon("💡").setUnit("%"));

MicroProto::Property<float> speed("speed", 1.0f, MicroProto::PropertyLevel::LOCAL,
    MicroProto::Constraints<float>().min(0.1f).max(10.0f).step(0.1f),
    "Animation speed multiplier",
    MicroProto::UIHints().setColor(MicroProto::UIColor::CYAN).setIcon("⚡").setUnit("x"));

MicroProto::Property<bool> enabled("enabled", true, MicroProto::PropertyLevel::LOCAL,
    "Enable/disable LED output",
    MicroProto::UIHints().setColor(MicroProto::UIColor::EMERALD).setIcon("🔌"));

// Container properties - ARRAY (fixed size) with element constraints
MicroProto::ArrayProperty<uint8_t, 3> rgbColor("rgbColor", {255, 128, 0}, MicroProto::PropertyLevel::LOCAL,
    MicroProto::ArrayConstraints<uint8_t>().min(0).max(255),
    "Primary RGB color [R, G, B]",
    MicroProto::UIHints().setColor(MicroProto::UIColor::PINK).setIcon("🎨"));

MicroProto::ArrayProperty<int32_t, 2> position("position", {100, 200}, MicroProto::PropertyLevel::LOCAL,
    MicroProto::ArrayConstraints<int32_t>().min(-1000).max(1000),
    "Animation position [X, Y]",
    MicroProto::UIHints().setColor(MicroProto::UIColor::SKY).setIcon("📍").setUnit("px"));

// Container properties - LIST (variable size) with container and element constraints
MicroProto::StringProperty<32> deviceName("deviceName", "LED Strip", MicroProto::PropertyLevel::LOCAL,
    MicroProto::ListConstraints<uint8_t>().minLength(1).maxLength(32),
    "Device display name",
    MicroProto::UIHints().setColor(MicroProto::UIColor::VIOLET).setIcon("📛"));

MicroProto::ListProperty<uint8_t, 8> pattern("pattern", {10, 20, 30, 40}, MicroProto::PropertyLevel::LOCAL,
    MicroProto::ListConstraints<uint8_t>().minLength(1).maxLength(8).elementMin(0).elementMax(100),
    "Animation pattern values (max 8)",
    MicroProto::UIHints().setColor(MicroProto::UIColor::TEAL).setIcon("📊"));

void setup() {
    Serial.begin(115200);
    delay(600);  // crucial for wifi
    Serial.println("\n\n=== HTTP + MicroProto + WiFiMan ===");

    // Initialize property system
    MicroProto::PropertySystem::init();
    Serial.printf("Properties registered: %d\n", MicroProto::PropertySystem::getPropertyCount());

    // Configure WiFiManager
    wifiManager.setAPCredentials("LED-Setup", "");  // Open AP for setup
    wifiManager.setHostname("ledstrip");
    wifiManager.credentials().addNetwork("Citadel", "kekovino4ka", 100);  // Default network

    // Register app routes BEFORE wifiManager.begin() so they're available
    httpDispatcher.onGet("/", [](HttpRequest& req) {
        return HttpResponse::html(index_htm, index_htm_len);
    });

    httpDispatcher.onGet("/ping", [](HttpRequest& req) {
        return HttpResponse::text("pong");
    });

    httpDispatcher.onPost("/echo", [](HttpRequest& req) {
        return HttpResponse::json(req.body().toString());
    });

    // MicroProto demo page
    httpDispatcher.onGet("/proto", [](HttpRequest& req) {
        return HttpResponse::html(proto_htm, proto_htm_len);
    });

    httpDispatcher.onGet("/js/proto.js", [](HttpRequest& req) {
        return HttpResponse()
            .status(200)
            .contentType("application/javascript")
            .body(microproto_client_js, microproto_client_js_len);
    });

    // Start WiFiManager (registers /wifiman routes, handles WiFi connection)
    wifiManager.begin();

    // Callback when connected
    wifiManager.onConnected([](const String& ssid) {
        Serial.printf("Connected to %s, starting servers\n", ssid.c_str());
        WiFi.setSleep(false);  // Low latency mode
    });

    // Start HTTP server (works in both AP and STA mode)
    http.begin();
    Serial.println("HTTP server on port 80");

    // Start MicroProto binary WebSocket server
    protoServer.begin();
    Serial.println("MicroProto server on port 81");

    Serial.printf("Registered %d routes\n", httpDispatcher.routeCount());
    Serial.println("=== Ready ===");
}

void loop() {
    wifiManager.loop();
    protoServer.loop();
    http.loop();
    MicroProto::PropertySystem::loop();

    ledBrightness = ledBrightness + static_cast<uint8_t>(speed.get());

    static uint32_t last = 0;
    if (millis() - last > 10000) {
        last = millis();
        Serial.printf("Free heap: %lu, RSSI: %d dBm, Proto clients: %u, WiFi: %s\n",
                      ESP.getFreeHeap(), WiFi.RSSI(),
                      protoServer.connectedClients(),
                      wifiManager.getStateString().c_str());

        // Demo: toggle brightness periodically
        ledBrightness = (ledBrightness + 32) % 256;
        Serial.printf("Brightness now: %d\n", (uint8_t)ledBrightness);
    }
}


// ORIGINAL CODE COMMENTED OUT
/*
#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WiFiMan.h>
#include <NimBLEDevice.h>
#include <FastLED.h>

#include "animations/Anime.h"`
#include "web/WebServer.h"
#include "web/SocketController.h"
#include "ble/BleDeviceManager.hpp"
#include "input/EncoderInput.hpp"
#include "PropertySystem.h"

AsyncWebServer server(80);
WiFiMan::WiFiManager wifiManager(&server);

CallResult<void*> status(nullptr);

uint32_t loopTimestampMillis = 0;
uint32_t loopIteration = 0;

void setup() {
    delay(600);
    Serial.begin(115200);

    Serial.println("\n=== Memory Usage Tracking ===");
    Serial.printf("1. Boot: Free heap: %d bytes\n", ESP.getFreeHeap());

    // Initialize property system first (includes loadFromStorage)
    MicroProto::PropertySystem::init();
    Serial.printf("2. After PropertySystem: Free heap: %d bytes\n",
                  ESP.getFreeHeap());

    EncoderInput::init();
    Serial.printf("3. After EncoderInput: Free heap: %d bytes\n",
                  ESP.getFreeHeap());

    // BleDeviceManager::init();
    // BleDeviceManager::loadKnownDevices();
    // BleDeviceManager::startPeriodicScanning(30000);

    Serial.printf("4. After BleDeviceManager: Free heap: %d bytes\n",
                  ESP.getFreeHeap());

    // Initialize web server and routes
    WebServer::init(server, wifiManager);
    Serial.printf("5. After WebServer init: Free heap: %d bytes\n",
                  ESP.getFreeHeap());

    // Add health endpoint (specific to main.cpp status)
    server.on("/health", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send_P((int)status.getCode(), "text/plain",
                        status.getMessage().c_str());
    });

    ShaderStorage::init();
    Serial.printf("6. After ShaderStorage: Free heap: %d bytes\n",
                  ESP.getFreeHeap());

    status = Anime::connect();
    while (status.hasError()) {
        Serial.print("Error starting Anime: ");
        Serial.println(status.getMessage());
        delay(1000);
        status = Anime::connect();
    }
    Serial.printf("7. After Anime connect: Free heap: %d bytes\n",
                  ESP.getFreeHeap());

    WebServer::begin(server);
    Serial.printf("8. After WebServer begin: Free heap: %d bytes\n",
                  ESP.getFreeHeap());
    Serial.println("=== Setup Complete ===\n");
}

void loop() {
    static bool firstLoop = true;
    if (firstLoop) {
        Serial.printf("Loop start: Free heap: %d bytes\n",
                      ESP.getFreeHeap());
        firstLoop = false;
    }

    loopTimestampMillis = millis();
    loopIteration++;

    // Property system handles debounced saving
    MicroProto::PropertySystem::loop();
    yield();  // Give WiFi/network tasks time

    EncoderInput::loop();
    yield();

    // BleDeviceManager::loop();
    yield();

    wifiManager.loop();
    yield();

    SocketController::cleanUp();
    yield();

    status = Anime::draw();
    yield();  // Important: give time after potentially blocking FastLED.show()

    // Memory and WiFi monitoring - print every 10 seconds
    static uint32_t lastMemPrint = 0;
    if (millis() - lastMemPrint > 10000) {
        lastMemPrint = millis();
        int8_t rssi = WiFi.RSSI();
        Serial.printf(
            "Free heap: %d bytes, Min free: %d bytes, Largest block: %d "
            "bytes, WiFi RSSI: %d dBm\n",
            ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(),
            rssi);
    }
}
*/
