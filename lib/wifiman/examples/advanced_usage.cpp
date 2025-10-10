/*
 * WiFiMan - Advanced Usage Example
 *
 * This example demonstrates advanced features:
 * - Multiple network management
 * - Custom priority handling
 * - Manual network control
 * - Integration with existing services
 */

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <WiFiMan.h>

AsyncWebServer server(80);
WiFiMan::WiFiManager wifiManager(&server);

// Service state
bool servicesRunning = false;

void startServices() {
    if (servicesRunning) return;

    Serial.println("Starting application services...");

    // Start your application services here
    // - MQTT connection
    // - NTP time sync
    // - Cloud services
    // - etc.

    servicesRunning = true;
    Serial.println("Services started");
}

void stopServices() {
    if (!servicesRunning) return;

    Serial.println("Stopping application services...");

    // Stop your services gracefully
    // - Close connections
    // - Save state
    // - etc.

    servicesRunning = false;
    Serial.println("Services stopped");
}

void setup() {
    Serial.begin(115200);
    delay(100);

    Serial.println("\n=== WiFiMan Advanced Example ===");

    // Configure WiFi manager
    wifiManager.setAPCredentials("ESP32-Advanced", "configure");
    wifiManager.setHostname("esp32-advanced");
    wifiManager.setConnectionTimeout(20000);
    wifiManager.setRetryDelay(10000);

    // Optional: Only stay in AP mode for 5 minutes, then retry
    wifiManager.setAPTimeout(300000);

    // Note: WiFiMan always scans before connecting, so it only tries
    // networks that are actually available - no wasted connection attempts!

    // Add multiple networks with priorities
    auto& creds = wifiManager.credentials();

    // Home network (highest priority)
    creds.addNetwork("HomeWiFi", "homepassword", 100);

    // Work network (medium priority)
    creds.addNetwork("WorkWiFi", "workpassword", 50);

    // Mobile hotspot (lowest priority, fallback)
    creds.addNetwork("iPhone", "hotspotpass", 10);

    // Print saved networks
    Serial.println("\nSaved Networks:");
    auto sorted = creds.getSortedNetworks();
    for (const auto* net : sorted) {
        Serial.printf("  - %s (Priority: %d, Last RSSI: %d)\n",
                     net->ssid.c_str(), net->priority, net->lastRSSI);
    }

    // Callbacks for service management
    wifiManager.onConnected([](const String& ssid) {
        Serial.printf("\n✓ Connected to: %s\n", ssid.c_str());
        Serial.printf("  IP: %s, RSSI: %d dBm\n",
                     WiFi.localIP().toString().c_str(),
                     WiFi.RSSI());

        startServices();
    });

    wifiManager.onDisconnected([]() {
        Serial.println("\n✗ Disconnected from WiFi");
        stopServices();
    });

    wifiManager.onAPStarted([](const String& ssid) {
        Serial.printf("\n⚠ Access Point Active\n");
        Serial.printf("  SSID: %s\n", ssid.c_str());
        Serial.printf("  Password: configure\n");
        Serial.printf("  IP: http://%s\n", WiFi.softAPIP().toString().c_str());

        stopServices();
    });

    wifiManager.onAPClientConnected([](uint8_t numClients) {
        Serial.printf("AP Clients: %d\n", numClients);
    });

    // Custom API endpoints
    server.on("/api/wifi/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        String json = "{";
        json += "\"state\":\"" + wifiManager.getStateString() + "\",";
        json += "\"connected\":" + String(wifiManager.isConnected() ? "true" : "false") + ",";
        json += "\"ssid\":\"" + wifiManager.getCurrentSSID() + "\",";
        json += "\"ip\":\"" + wifiManager.getIP().toString() + "\",";
        json += "\"rssi\":" + String(WiFi.RSSI());
        json += "}";
        request->send(200, "application/json", json);
    });

    server.on("/api/wifi/disconnect", HTTP_POST, [](AsyncWebServerRequest *request) {
        wifiManager.disconnect();
        request->send(200, "application/json", "{\"success\":true}");
    });

    server.on("/api/wifi/retry", HTTP_POST, [](AsyncWebServerRequest *request) {
        wifiManager.retry();
        request->send(200, "application/json", "{\"success\":true}");
    });

    server.on("/api/wifi/ap-start", HTTP_POST, [](AsyncWebServerRequest *request) {
        wifiManager.startAP();
        request->send(200, "application/json", "{\"success\":true}");
    });

    // Start everything
    wifiManager.begin();
    server.begin();

    Serial.println("\nSetup complete!");
    Serial.println("Custom API available at:");
    Serial.println("  GET  /api/wifi/status");
    Serial.println("  POST /api/wifi/disconnect");
    Serial.println("  POST /api/wifi/retry");
    Serial.println("  POST /api/wifi/ap-start");
}

void loop() {
    wifiManager.loop();

    // Application logic
    if (servicesRunning) {
        // Your periodic tasks here
        // - Send sensor data
        // - Update displays
        // - Process queues
        // etc.
    }

    // Monitor and respond to WiFi state changes
    static WiFiMan::State lastState = WiFiMan::State::IDLE;
    WiFiMan::State currentState = wifiManager.getState();

    if (currentState != lastState) {
        Serial.printf("State changed: %s -> %s\n",
                     lastState == WiFiMan::State::IDLE ? "IDLE" :
                     lastState == WiFiMan::State::SCANNING ? "SCANNING" :
                     lastState == WiFiMan::State::CONNECTING ? "CONNECTING" :
                     lastState == WiFiMan::State::CONNECTED ? "CONNECTED" :
                     lastState == WiFiMan::State::AP_MODE ? "AP_MODE" : "FAILED",
                     wifiManager.getStateString().c_str());
        lastState = currentState;
    }

    // Example: Force AP mode with a button press
    // (You would read an actual button here)
    static bool buttonPressed = false;
    if (buttonPressed && !wifiManager.isAPMode()) {
        Serial.println("Button pressed - starting AP mode");
        wifiManager.startAP();
        buttonPressed = false;
    }

    // Status monitoring
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint > 30000) {
        lastPrint = millis();
        Serial.printf("\n--- Status ---\n");
        Serial.printf("State: %s\n", wifiManager.getStateString().c_str());
        Serial.printf("Services: %s\n", servicesRunning ? "Running" : "Stopped");
        Serial.printf("Heap: %d bytes\n", ESP.getFreeHeap());
        if (wifiManager.isConnected()) {
            Serial.printf("WiFi: %s (%d dBm)\n",
                         WiFi.SSID().c_str(), WiFi.RSSI());
        }
        Serial.println("-------------\n");
    }
}
