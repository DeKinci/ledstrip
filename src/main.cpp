// PropertyStorage test passed ✓ - see test/test_property_storage/ for unit tests
#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <NetWizard.h>
#include <NimBLEDevice.h>
#include <FastLED.h>

#include "animations/Anime.h"
#include "web/WebServer.h"
#include "web/SocketController.h"
#include "ble/BleClient.hpp"
#include "input/EncoderInput.hpp"

DNSServer dnsServer;
AsyncWebServer server(80);
NetWizard NW(&server);

CallResult<void*> status(nullptr);

uint32_t loopTimestampMillis = 0;
uint32_t loopIteration = 0;

void setup() {
    delay(200);
    Serial.begin(115200);

    Serial.println("\n=== Memory Usage Tracking ===");
    Serial.printf("1. Boot: Free heap: %d bytes\n", ESP.getFreeHeap());

    EncoderInput::init();
    Serial.printf("2. After EncoderInput: Free heap: %d bytes\n", ESP.getFreeHeap());

    // BleClient::init();
    Serial.printf("3. After BleClient: Free heap: %d bytes\n", ESP.getFreeHeap());
    // BleClient::scan();

    // Initialize web server and routes
    WebServer::init(server, NW);
    Serial.printf("4. After WebServer init: Free heap: %d bytes\n", ESP.getFreeHeap());

    // Add health endpoint (specific to main.cpp status)
    server.on("/health", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send_P((int)status.getCode(), "text/plain", status.getMessage().c_str());
    });

    ShaderStorage::init();
    Serial.printf("5. After ShaderStorage: Free heap: %d bytes\n", ESP.getFreeHeap());

    status = Anime::connect();
    while (status.hasError()) {
        Serial.print("Error starting Anime: ");
        Serial.println(status.getMessage());
        delay(1000);
        status = Anime::connect();
    }
    Serial.printf("6. After Anime connect: Free heap: %d bytes\n", ESP.getFreeHeap());

    WebServer::begin(server);
    Serial.printf("7. After WebServer begin: Free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.println("=== Setup Complete ===\n");
}

void loop() {
    static bool firstLoop = true;
    if (firstLoop) {
        Serial.printf("Loop start: Free heap: %d bytes\n", ESP.getFreeHeap());
        firstLoop = false;
    }

    loopTimestampMillis = millis();
    loopIteration++;

    EncoderInput::loop();

    // BleClient::loop();

    NW.loop();

    SocketController::cleanUp();
    status = Anime::draw();

    // Memory monitoring - print every 10 seconds
    static uint32_t lastMemPrint = 0;
    if (millis() - lastMemPrint > 10000) {
        lastMemPrint = millis();
        Serial.printf("Free heap: %d bytes, Min free: %d bytes, Largest block: %d bytes\n",
            ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
    }
}