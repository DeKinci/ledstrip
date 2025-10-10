/*
 * WiFiMan - Basic Usage Example
 *
 * This example demonstrates the basic setup and usage of WiFiMan.
 * The device will:
 * 1. Try to connect to saved networks
 * 2. If no networks are saved or connection fails, start an AP
 * 3. Provide a captive portal for configuration
 */

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <WiFiMan.h>

// Create web server on port 80
AsyncWebServer server(80);

// Create WiFi manager with server
WiFiMan::WiFiManager wifiManager(&server);

void setup() {
    Serial.begin(115200);
    delay(100);

    Serial.println("\n=== WiFiMan Basic Example ===");

    // Configure AP settings (shown when no WiFi is available)
    wifiManager.setAPCredentials("ESP32-Setup", "");  // No password

    // Set device hostname
    wifiManager.setHostname("esp32-demo");

    // Configure timeouts
    wifiManager.setConnectionTimeout(15000);  // 15 seconds per network
    wifiManager.setRetryDelay(5000);          // 5 seconds between retries

    // Optional: Programmatically add a network
    // wifiManager.credentials().addNetwork("YourWiFi", "YourPassword", 100);

    // Register callbacks
    wifiManager.onConnected([](const String& ssid) {
        Serial.printf("\n✓ Connected to: %s\n", ssid.c_str());
        Serial.printf("  IP Address: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("  Signal: %d dBm\n", WiFi.RSSI());

        // Your app initialization here
        // e.g., start services, sync time, etc.
    });

    wifiManager.onDisconnected([]() {
        Serial.println("\n✗ WiFi disconnected");
        // Your cleanup code here
    });

    wifiManager.onAPStarted([](const String& ssid) {
        Serial.printf("\n⚠ AP Mode Started\n");
        Serial.printf("  SSID: %s\n", ssid.c_str());
        Serial.printf("  IP: %s\n", WiFi.softAPIP().toString().c_str());
        Serial.println("  Connect to this network to configure WiFi");
    });

    // Start WiFi manager (non-blocking)
    wifiManager.begin();

    // Add your own API routes
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"status\":\"running\"}");
    });

    // Start web server
    server.begin();

    Serial.println("Setup complete!\n");
}

void loop() {
    // REQUIRED: Call this every loop iteration
    wifiManager.loop();

    // Your application code here
    // This continues to run regardless of WiFi state

    // Example: Print status every 10 seconds
    static uint32_t lastStatus = 0;
    if (millis() - lastStatus > 10000) {
        lastStatus = millis();
        Serial.printf("WiFi State: %s | Heap: %d bytes\n",
                     wifiManager.getStateString().c_str(),
                     ESP.getFreeHeap());
    }
}
