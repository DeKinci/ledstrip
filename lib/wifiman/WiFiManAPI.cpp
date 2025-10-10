#include "WiFiMan.h"
#include "WiFiManWebUI.h"
#include <ArduinoJson.h>
#include <AsyncJson.h>

namespace WiFiMan {

void WiFiManager::setupWebServer() {
    if (!webServer) {
        Serial.println("[WiFiMan] No web server provided, captive portal disabled");
        return;
    }

    Serial.println("[WiFiMan] Setting up WiFiMan web interface");

    // Register API routes FIRST (before /wifiman page route to avoid prefix matching)

    // API: Scan networks
    webServer->on("/wifiman/scan", HTTP_GET, [this](AsyncWebServerRequest *request) {
        Serial.println("[WiFiMan] Scan endpoint called");
        JsonDocument doc;
        JsonArray networks = doc["networks"].to<JsonArray>();

        int n = WiFi.scanComplete();
        Serial.printf("[WiFiMan] scanComplete() returned: %d\n", n);

        if (n == WIFI_SCAN_RUNNING) {
            doc["status"] = "scanning";
            Serial.println("[WiFiMan] Scan already running");
        } else if (n >= 0) {
            Serial.printf("[WiFiMan] Found %d networks\n", n);
            for (int i = 0; i < n; i++) {
                JsonObject net = networks.add<JsonObject>();
                net["ssid"] = WiFi.SSID(i);
                net["rssi"] = WiFi.RSSI(i);
                net["encrypted"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
            }
            WiFi.scanDelete();
            WiFi.scanNetworks(true);  // Start new async scan
        } else {
            // Start initial scan
            Serial.println("[WiFiMan] Starting new scan");
            WiFi.scanNetworks(true);
            doc["status"] = "scanning";
        }

        String response;
        serializeJson(doc, response);
        Serial.printf("[WiFiMan] Sending response: %s\n", response.c_str());
        request->send(200, "application/json", response);
    });

    // API: List saved networks
    webServer->on("/wifiman/list", HTTP_GET, [this](AsyncWebServerRequest *request) {
        Serial.println("[WiFiMan] List endpoint called");
        JsonDocument doc;
        JsonArray networks = doc["networks"].to<JsonArray>();

        auto allCreds = creds.getAll();
        Serial.printf("[WiFiMan] Found %d saved networks\n", allCreds.size());

        for (const auto& cred : allCreds) {
            JsonObject net = networks.add<JsonObject>();
            net["ssid"] = cred.ssid;
            net["priority"] = cred.priority;
            net["lastRSSI"] = cred.lastRSSI;
            net["lastConnected"] = cred.lastConnected;
        }

        String response;
        serializeJson(doc, response);
        Serial.printf("[WiFiMan] Sending response: %s\n", response.c_str());
        request->send(200, "application/json", response);
    });

    // API: Status
    webServer->on("/wifiman/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        JsonDocument doc;
        doc["state"] = getStateString();
        doc["connected"] = isConnected();
        doc["ssid"] = getCurrentSSID();
        doc["ip"] = getIP().toString();
        doc["apMode"] = isAPMode();

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: Clear all networks
    webServer->on("/wifiman/clear", HTTP_POST, [this](AsyncWebServerRequest *request) {
        creds.clearAll();
        request->send(200, "application/json", "{\"success\":true}");
        Serial.println("[WiFiMan] All networks cleared via web");
    });

    // API: Connect now
    webServer->on("/wifiman/connect", HTTP_POST, [this](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"success\":true}");
        Serial.println("[WiFiMan] Connection requested via web");

        // Set flag to trigger connection in loop (after response is sent)
        webConnectRequestTime = millis();
    });

    // API: Add network (using AsyncCallbackJsonWebHandler)
    AsyncCallbackJsonWebHandler* addHandler = new AsyncCallbackJsonWebHandler(
        "/wifiman/add",
        [this](AsyncWebServerRequest *request, JsonVariant &json) {
            JsonObject obj = json.as<JsonObject>();

            if (!obj.containsKey("ssid")) {
                request->send(400, "application/json", "{\"error\":\"SSID required\"}");
                return;
            }

            String ssid = obj["ssid"].as<String>();
            String password = obj["password"] | "";
            int priority = obj["priority"] | 0;

            if (creds.addNetwork(ssid, password, priority)) {
                request->send(200, "application/json", "{\"success\":true}");
                Serial.printf("[WiFiMan] Network added via web: %s\n", ssid.c_str());
            } else {
                request->send(500, "application/json", "{\"error\":\"Failed to add network\"}");
            }
        }
    );
    webServer->addHandler(addHandler);

    // API: Remove network (using AsyncCallbackJsonWebHandler)
    AsyncCallbackJsonWebHandler* removeHandler = new AsyncCallbackJsonWebHandler(
        "/wifiman/remove",
        [this](AsyncWebServerRequest *request, JsonVariant &json) {
            JsonObject obj = json.as<JsonObject>();

            if (!obj.containsKey("ssid")) {
                request->send(400, "application/json", "{\"error\":\"SSID required\"}");
                return;
            }

            String ssid = obj["ssid"].as<String>();

            if (creds.removeNetwork(ssid)) {
                request->send(200, "application/json", "{\"success\":true}");
                Serial.printf("[WiFiMan] Network removed via web: %s\n", ssid.c_str());
            } else {
                request->send(404, "application/json", "{\"error\":\"Network not found\"}");
            }
        }
    );
    webServer->addHandler(removeHandler);

    // Main portal page - captive portal at / (for AP mode) and /wifiman (always available)
    // IMPORTANT: Register this AFTER all /wifiman/* API routes to avoid prefix matching
    webServer->on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
        request->send_P(200, "text/html", WIFIMAN_PORTAL_HTML);
    });

    webServer->on("/wifiman", HTTP_GET, [this](AsyncWebServerRequest *request) {
        request->send_P(200, "text/html", WIFIMAN_PORTAL_HTML);
    });

    // Captive portal detection endpoints
    // Android
    webServer->on("/generate_204", HTTP_GET, [this](AsyncWebServerRequest *request) {
        request->redirect("/");
    });
    webServer->on("/gen_204", HTTP_GET, [this](AsyncWebServerRequest *request) {
        request->redirect("/");
    });
    // iOS/macOS
    webServer->on("/hotspot-detect.html", HTTP_GET, [this](AsyncWebServerRequest *request) {
        request->redirect("/");
    });
    // Windows
    webServer->on("/connecttest.txt", HTTP_GET, [this](AsyncWebServerRequest *request) {
        request->redirect("/");
    });
    webServer->on("/ncsi.txt", HTTP_GET, [this](AsyncWebServerRequest *request) {
        request->redirect("/");
    });
    // Additional Android endpoints
    webServer->on("/mobile/status.php", HTTP_GET, [this](AsyncWebServerRequest *request) {
        request->redirect("/");
    });
    webServer->on("/canonical.html", HTTP_GET, [this](AsyncWebServerRequest *request) {
        request->redirect("/");
    });
    webServer->on("/success.txt", HTTP_GET, [this](AsyncWebServerRequest *request) {
        request->redirect("/");
    });

    Serial.println("[WiFiMan] Web interface ready at / and /wifiman");
}

void WiFiManager::teardownWebServer() {
    // Web routes stay active - available at http://device-ip/wifiman when connected
    Serial.println("[WiFiMan] Web interface remains available for reconfiguration");
}

} // namespace WiFiMan
