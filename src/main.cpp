// MINIMAL TEST - Direct routing like latency test
#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsServer.h>
#include "rsc/w_index_htm.h"

WiFiServer server(80);
WebSocketsServer webSocket(81);

const char* ssid = "Citadel";
const char* password = "kekovino4ka";

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

static bool readHeaders(WiFiClient& client, String& request,
                        uint32_t firstByteDeadlineMs = 5000,
                        uint32_t headerDeadlineMs = 500) {
    uint32_t t0 = millis();

    // Wait for first byte (longer deadline)
    while (client.connected() && !client.available()) {
        if (millis() - t0 > firstByteDeadlineMs)
            return false;  // no data
        webSocket.loop();  // Keep WebSocket responsive
        delay(1);
        yield();
    }

    // First byte arrived, now finish headers (shorter deadline)
    uint32_t tFirst = millis();
    while (client.connected() && millis() - tFirst < headerDeadlineMs) {
        while (client.available()) {
            char c = client.read();
            request += c;
            if (request.endsWith("\r\n\r\n"))
                return true;
        }
        webSocket.loop();  // Keep WebSocket responsive
        delay(1);
        yield();
    }
    return request.endsWith("\r\n\r\n");
}

void setup() {
    Serial.begin(115200);
    delay(600);  // crucial for wifi
    Serial.println("\n\n=== MINIMAL HTTP + WebSocket ===");

    WiFi.begin();
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    WiFi.setSleep(false);  // <--- low latency
    WiFi.begin(ssid, password);

    Serial.printf("Connecting to %s", ssid);
    while (WiFi.status() != WL_CONNECTED) {
        delay(250);
        Serial.print(".");
    }
    Serial.printf("\n✓ WiFi Connected: %s  RSSI=%d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());

    server.begin();
    Serial.println("✓ HTTP server on port 80");

    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    Serial.println("✓ WebSocket on port 81");

    Serial.println("=== Ready ===");
}

void loop() {
    webSocket.loop();  // Always process WebSocket events

    WiFiClient client = server.available();
    if (!client) {
        static uint32_t last = 0;
        if (millis() - last > 10000) {
            last = millis();
            Serial.printf("Free heap: %u, RSSI: %d dBm, WS clients: %u\n",
                          ESP.getFreeHeap(), WiFi.RSSI(),
                          webSocket.connectedClients());
        }
        return;
    }

    client.setNoDelay(true);  // <--- avoid extra RTTs
    uint32_t tStart = millis();
    Serial.printf("%u  Request started\n", tStart);

    String req;
    bool got = readHeaders(client, req);
    Serial.printf("Request: %s\n", req.c_str());

    if (got && req.startsWith("GET / ")) {
        client.print(
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Connection: close\r\n");
        client.printf("Content-Length: %d\r\n\r\n", index_htm_len);
        client.write(index_htm, index_htm_len);
    } else if (got && req.startsWith("GET /ping ")) {
        client.print(
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 4\r\n"
            "Connection: close\r\n"
            "\r\n");
        client.write("pong", 4);  // exactly 4 bytes
    } else if (req.length()) {
        client.print(
            "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n");
    } else {
        // No headers received — best to 408 the client to be explicit
        client.print(
            "HTTP/1.1 408 Request Timeout\r\nConnection: close\r\n\r\n");
    }

    client.stop();
    Serial.printf("%u  Request done (%u ms)\n", millis(), millis() - tStart);
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
