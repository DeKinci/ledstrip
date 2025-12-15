#include "WiFiMan.h"
#include "WiFiManWebUI.h"

namespace WiFiMan {

void WiFiManager::setupRoutes() {
    if (!_dispatcher) {
        Serial.println("[WiFiMan] No dispatcher provided, web interface disabled");
        return;
    }

    Serial.println("[WiFiMan] Setting up WiFiMan web routes");

    // API: Scan networks
    _dispatcher->onGet("/wifiman/scan", [this](HttpRequest& req) -> HttpResponse {
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

        return HttpResponse::json(doc);
    });

    // API: List saved networks
    _dispatcher->onGet("/wifiman/list", [this](HttpRequest& req) -> HttpResponse {
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

        return HttpResponse::json(doc);
    });

    // API: Status
    _dispatcher->onGet("/wifiman/status", [this](HttpRequest& req) -> HttpResponse {
        JsonDocument doc;
        doc["state"] = getStateString();
        doc["connected"] = isConnected();
        doc["ssid"] = getCurrentSSID();
        doc["ip"] = getIP().toString();
        doc["apMode"] = isAPMode();
        String err = getLastError();
        if (err.length() > 0) {
            doc["error"] = err;
        }

        return HttpResponse::json(doc);
    });

    // API: Add network
    _dispatcher->onPost("/wifiman/add", [this](HttpRequest& req) -> HttpResponse {
        JsonDocument doc;
        if (!req.json(doc)) {
            return HttpResponse::json("{\"error\":\"Invalid JSON\"}", 400);
        }

        if (!doc.containsKey("ssid")) {
            return HttpResponse::json("{\"error\":\"SSID required\"}", 400);
        }

        String ssid = doc["ssid"].as<String>();
        String password = doc["password"] | "";
        int priority = doc["priority"] | 0;

        if (creds.addNetwork(ssid, password, priority)) {
            Serial.printf("[WiFiMan] Network added via web: %s\n", ssid.c_str());
            return HttpResponse::json("{\"success\":true}");
        } else {
            return HttpResponse::json("{\"error\":\"Failed to add network\"}", 500);
        }
    });

    // API: Remove network
    _dispatcher->onPost("/wifiman/remove", [this](HttpRequest& req) -> HttpResponse {
        JsonDocument doc;
        if (!req.json(doc)) {
            return HttpResponse::json("{\"error\":\"Invalid JSON\"}", 400);
        }

        if (!doc.containsKey("ssid")) {
            return HttpResponse::json("{\"error\":\"SSID required\"}", 400);
        }

        String ssid = doc["ssid"].as<String>();

        if (creds.removeNetwork(ssid)) {
            Serial.printf("[WiFiMan] Network removed via web: %s\n", ssid.c_str());
            return HttpResponse::json("{\"success\":true}");
        } else {
            return HttpResponse::json("{\"error\":\"Network not found\"}", 404);
        }
    });

    // API: Clear all networks
    _dispatcher->onPost("/wifiman/clear", [this](HttpRequest& req) -> HttpResponse {
        creds.clearAll();
        Serial.println("[WiFiMan] All networks cleared via web");
        return HttpResponse::json("{\"success\":true}");
    });

    // API: Connect now
    _dispatcher->onPost("/wifiman/connect", [this](HttpRequest& req) -> HttpResponse {
        Serial.println("[WiFiMan] Connection requested via web");
        // Set flag to trigger connection in loop (after response is sent)
        webConnectRequestTime = millis();
        return HttpResponse::json("{\"success\":true}");
    });

    // Permanent portal page at /wifiman (always available)
    _dispatcher->onGet("/wifiman", [](HttpRequest& req) -> HttpResponse {
        return HttpResponse::html((const uint8_t*)WIFIMAN_PORTAL_HTML, strlen(WIFIMAN_PORTAL_HTML));
    });

    Serial.println("[WiFiMan] Web routes ready at /wifiman");
}

void WiFiManager::setupCaptivePortal() {
    if (!_dispatcher) return;

    Serial.println("[WiFiMan] Setting up captive portal routes");

    // High-priority route for / that serves portal in AP mode
    // Priority 100 ensures this takes precedence over normal "/" route
    _captiveRootHandle = _dispatcher->onGet("/", [](HttpRequest& req) -> HttpResponse {
        return HttpResponse::html((const uint8_t*)WIFIMAN_PORTAL_HTML, strlen(WIFIMAN_PORTAL_HTML));
    }, 100);

    _captiveDetectCount = 0;

    // Helper for redirect response
    auto redirect = [](HttpRequest& req) -> HttpResponse {
        return HttpResponse().status(302).header("Location", "/").body("");
    };

    // Captive portal detection endpoints - all redirect to portal
    // Android
    _captiveDetectHandles[_captiveDetectCount++] = _dispatcher->onGet("/generate_204", redirect, 100);
    _captiveDetectHandles[_captiveDetectCount++] = _dispatcher->onGet("/gen_204", redirect, 100);

    // iOS/macOS
    _captiveDetectHandles[_captiveDetectCount++] = _dispatcher->onGet("/hotspot-detect.html", redirect, 100);

    // Windows
    _captiveDetectHandles[_captiveDetectCount++] = _dispatcher->onGet("/connecttest.txt", redirect, 100);
    _captiveDetectHandles[_captiveDetectCount++] = _dispatcher->onGet("/ncsi.txt", redirect, 100);

    // Additional Android endpoints
    _captiveDetectHandles[_captiveDetectCount++] = _dispatcher->onGet("/mobile/status.php", redirect, 100);
    _captiveDetectHandles[_captiveDetectCount++] = _dispatcher->onGet("/canonical.html", redirect, 100);
    _captiveDetectHandles[_captiveDetectCount++] = _dispatcher->onGet("/success.txt", redirect, 100);

    Serial.printf("[WiFiMan] Captive portal ready with %d detection endpoints\n", _captiveDetectCount + 1);
}

void WiFiManager::teardownCaptivePortal() {
    if (!_dispatcher) return;

    Serial.println("[WiFiMan] Removing captive portal routes");

    // Remove root captive portal
    if (_captiveRootHandle.valid()) {
        _dispatcher->off(_captiveRootHandle);
        _captiveRootHandle = HttpDispatcher::RouteHandle::invalid();
    }

    // Remove detection endpoints
    for (int i = 0; i < _captiveDetectCount; i++) {
        if (_captiveDetectHandles[i].valid()) {
            _dispatcher->off(_captiveDetectHandles[i]);
            _captiveDetectHandles[i] = HttpDispatcher::RouteHandle::invalid();
        }
    }
    _captiveDetectCount = 0;

    Serial.println("[WiFiMan] Captive portal removed, /wifiman still available");
}

} // namespace WiFiMan