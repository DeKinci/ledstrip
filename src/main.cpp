// MINIMAL TEST - Direct routing like latency test
#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsServer.h>
#include <HttpServer.h>
#include <WiFiMan.h>
#include "rsc/w_index_htm.h"

HttpServer http(80);
WebSocketsServer webSocket(81);
WiFiMan::WiFiManager wifiManager(&httpDispatcher);

void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload,
                    size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            Serial.printf("[WS] Client %u disconnected\n", num);
            break;
        case WStype_CONNECTED: {
            IPAddress ip = webSocket.remoteIP(num);
            Serial.printf("[WS] Client %u connected from %s\n", num,
                          ip.toString().c_str());
        } break;
        case WStype_TEXT:
            Serial.printf("[WS] Client %u: %s\n", num, payload);
            webSocket.broadcastTXT(payload, length);
            break;
        default:
            break;
    }
}

void setup() {
    Serial.begin(115200);
    delay(600);  // crucial for wifi
    Serial.println("\n\n=== HTTP + WebSocket + WiFiMan ===");

    // Configure WiFiManager
    wifiManager.setAPCredentials("LED-Setup", "");  // Open AP for setup
    wifiManager.setHostname("ledstrip");
    wifiManager.credentials().addNetwork("Citadel", "kekovino4ka", 100);  // Default network

    // Register app routes BEFORE wifiManager.begin() so they're available
    // Note: "/" has priority 0, captive portal uses priority 100 to override in AP mode
    httpDispatcher.onGet("/", [](HttpRequest& req) {
        return HttpResponse::html(index_htm, index_htm_len);
    });

    httpDispatcher.onGet("/ping", [](HttpRequest& req) {
        return HttpResponse::text("pong");
    });

    httpDispatcher.onPost("/echo", [](HttpRequest& req) {
        return HttpResponse::json(req.body().toString());
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

    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    Serial.println("WebSocket on port 81");

    Serial.printf("Registered %d routes\n", httpDispatcher.routeCount());
    Serial.println("=== Ready ===");
}

void loop() {
    wifiManager.loop();
    webSocket.loop();
    http.loop();

    static uint32_t last = 0;
    if (millis() - last > 10000) {
        last = millis();
        Serial.printf("Free heap: %lu, RSSI: %d dBm, WS clients: %u, WiFi: %s\n",
                      ESP.getFreeHeap(), WiFi.RSSI(),
                      webSocket.connectedClients(),
                      wifiManager.getStateString().c_str());
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
