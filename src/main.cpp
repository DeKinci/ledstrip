// LED Strip Controller - HTTP + MicroProto + Anime
#include <Arduino.h>
#include <WiFi.h>
#include <HttpServer.h>
#include <WiFiMan.h>
#include <PropertySystem.h>
#include <transport/MicroProtoServer.h>

#include "animations/Anime.h"
#include "core/ShaderStorage.h"
#include "web/LedApiController.h"
#include "input/EncoderInput.hpp"
#include "ble/BleDeviceManager.hpp"

#include "rsc/w_index_htm.h"
#include "rsc/w_proto_htm.h"
#include "rsc/w_ble_htm.h"
#include "rsc/w_microproto_client_js.h"

HttpServer http(80);
MicroProto::MicroProtoServer protoServer(81);
WiFiMan::WiFiManager wifiManager(&httpDispatcher);

CallResult<void*> animeStatus(nullptr);

void setup() {
    Serial.begin(115200);
    delay(600);  // crucial for wifi
    Serial.println("\n\n=== LED Strip Controller ===");
    Serial.printf("1. Boot: Free heap: %lu bytes\n", ESP.getFreeHeap());

    // Initialize BLE FIRST (before WiFi) - they share the radio
    BleDeviceManager::init();
    Serial.printf("2. After BLE init: Free heap: %lu bytes\n", ESP.getFreeHeap());

    // Initialize property system (includes loading persistent values)
    MicroProto::PropertySystem::init();
    Serial.printf("2. After PropertySystem: Free heap: %lu bytes, Properties: %d\n",
                  ESP.getFreeHeap(), MicroProto::PropertySystem::getPropertyCount());

    // Initialize encoder input
    EncoderInput::init();
    Serial.printf("3. After EncoderInput: Free heap: %lu bytes\n", ESP.getFreeHeap());

    // Initialize shader storage
    ShaderStorage::init();
    Serial.printf("4. After ShaderStorage: Free heap: %lu bytes\n", ESP.getFreeHeap());

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

    // Register static page routes
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

    // BLE device management page
    httpDispatcher.onGet("/ble", [](HttpRequest& req) {
        return HttpResponse::html(ble_htm, ble_htm_len);
    });

    httpDispatcher.onGet("/js/proto.js", [](HttpRequest& req) {
        return HttpResponse()
            .status(200)
            .contentType("application/javascript")
            .body(microproto_client_js, microproto_client_js_len);
    });

    // Register LED API routes (shader CRUD, animation control)
    LedApiController::registerRoutes(httpDispatcher);

    // Start WiFiManager (registers /wifiman routes, handles WiFi connection)
    wifiManager.begin();

    // Callback when connected
    wifiManager.onConnected([](const String& ssid) {
        Serial.printf("Connected to %s\n", ssid.c_str());
        WiFi.setSleep(false);  // Low latency mode
    });

    // Start HTTP server (works in both AP and STA mode)
    http.begin();
    Serial.println("HTTP server on port 80");

    // Start MicroProto binary WebSocket server
    protoServer.begin();
    Serial.println("MicroProto server on port 81");

    Serial.printf("6. Setup complete: Free heap: %lu bytes, Routes: %d\n",
                  ESP.getFreeHeap(), httpDispatcher.routeCount());
    Serial.println("=== Ready ===");
}

void loop() {
    // Network services
    wifiManager.loop();
    protoServer.loop();
    http.loop();

    // Property system (handles dirty tracking and persistence)
    MicroProto::PropertySystem::loop();
    yield();

    // Physical input
    EncoderInput::loop();
    yield();

    // BLE device management
    BleDeviceManager::loop();
    yield();

    // LED animation
    animeStatus = Anime::draw();
    yield();

    // Periodic status logging
    static uint32_t lastStatusPrint = 0;
    if (millis() - lastStatusPrint > 10000) {
        lastStatusPrint = millis();
        Serial.printf("Free heap: %lu, RSSI: %d dBm, Proto clients: %u, WiFi: %s, Shaders: %u\n",
                      ESP.getFreeHeap(), WiFi.RSSI(),
                      protoServer.connectedClients(),
                      wifiManager.getStateString().c_str(),
                      Anime::getShaderCount());
    }
}
