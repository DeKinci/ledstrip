#include "WiFiMan.h"
#include "gen/w_portal_htm.h"
#include <MicroLog.h>
#include <ResponseBuffer.h>

static const char* TAG = "WiFiMan";

// Helper: serialize ArduinoJson doc into ResponseBuffer
static HttpResponse jsonResponse(const JsonDocument& doc, ResponseBuffer& buf, int code = 200) {
    char* start = buf.writePtr();
    size_t len = serializeJson(doc, start, buf.remaining());
    buf.advance(len);
    return HttpResponse::json(start, len, code);
}

namespace WiFiMan {

void WiFiManager::setupRoutes() {
    if (!_dispatcher) {
        LOG_WARN(TAG, "No dispatcher provided, web interface disabled");
        return;
    }

    LOG_INFO(TAG, "Setting up WiFiMan web routes");

    // API: Scan networks
    _dispatcher->onGet("/wifiman/scan", [this](HttpRequest& req, ResponseBuffer& buf) -> HttpResponse {
        LOG_DEBUG(TAG, "Scan endpoint called");
        JsonDocument doc;
        JsonArray networks = doc["networks"].to<JsonArray>();

        int n = WiFi.scanComplete();

        if (n == WIFI_SCAN_RUNNING) {
            doc["status"] = "scanning";
        } else if (n >= 0) {
            for (int i = 0; i < n; i++) {
                JsonObject net = networks.add<JsonObject>();
                net["ssid"] = WiFi.SSID(i);
                net["rssi"] = WiFi.RSSI(i);
                net["encrypted"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
            }
            WiFi.scanDelete();
            WiFi.scanNetworks(true);
        } else {
            WiFi.scanNetworks(true);
            doc["status"] = "scanning";
        }

        return jsonResponse(doc, buf);
    });

    // API: List saved networks
    _dispatcher->onGet("/wifiman/list", [this](HttpRequest& req, ResponseBuffer& buf) -> HttpResponse {
        JsonDocument doc;
        JsonArray networks = doc["networks"].to<JsonArray>();

        for (const auto& cred : creds.getAll()) {
            JsonObject net = networks.add<JsonObject>();
            net["ssid"] = cred.ssid;
            net["priority"] = cred.priority;
            net["lastRSSI"] = cred.lastRSSI;
            net["lastConnected"] = cred.lastConnected;
        }

        return jsonResponse(doc, buf);
    });

    // API: Status
    _dispatcher->onGet("/wifiman/status", [this](HttpRequest& req, ResponseBuffer& buf) -> HttpResponse {
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

        return jsonResponse(doc, buf);
    });

    // API: Add network
    _dispatcher->onPost("/wifiman/add", [this](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
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
            return HttpResponse::json("{\"success\":true}");
        } else {
            return HttpResponse::json("{\"error\":\"Failed to add network\"}", 500);
        }
    });

    // API: Remove network
    _dispatcher->onPost("/wifiman/remove", [this](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        JsonDocument doc;
        if (!req.json(doc)) {
            return HttpResponse::json("{\"error\":\"Invalid JSON\"}", 400);
        }

        if (!doc.containsKey("ssid")) {
            return HttpResponse::json("{\"error\":\"SSID required\"}", 400);
        }

        String ssid = doc["ssid"].as<String>();

        if (creds.removeNetwork(ssid)) {
            return HttpResponse::json("{\"success\":true}");
        } else {
            return HttpResponse::json("{\"error\":\"Network not found\"}", 404);
        }
    });

    // API: Clear all networks
    _dispatcher->onPost("/wifiman/clear", [this](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        creds.clearAll();
        return HttpResponse::json("{\"success\":true}");
    });

    // API: Connect now
    _dispatcher->onPost("/wifiman/connect", [this](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        webConnectRequestTime = millis();
        return HttpResponse::json("{\"success\":true}");
    });

    // Permanent portal page
    _dispatcher->resource("/wifiman", portal_htm);

    LOG_INFO(TAG, "Web routes ready at /wifiman");
}

void WiFiManager::setupCaptivePortal() {
    if (!_dispatcher) return;

    LOG_INFO(TAG, "Setting up captive portal routes");

    _captiveRootHandle = _dispatcher->onGet("/", [](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        return HttpResponse().status(200).contentType(portal_htm.contentType).body(portal_htm.data, portal_htm.length);
    }, 100);

    _captiveDetectCount = 0;

    auto redirect = [](HttpRequest& req, ResponseBuffer& buf) -> HttpResponse {
        return HttpResponse().status(302).header("Location", "/", buf).body("");
    };

    // Captive portal detection endpoints
    _captiveDetectHandles[_captiveDetectCount++] = _dispatcher->onGet("/generate_204", redirect, 100);
    _captiveDetectHandles[_captiveDetectCount++] = _dispatcher->onGet("/gen_204", redirect, 100);
    _captiveDetectHandles[_captiveDetectCount++] = _dispatcher->onGet("/hotspot-detect.html", redirect, 100);
    _captiveDetectHandles[_captiveDetectCount++] = _dispatcher->onGet("/connecttest.txt", redirect, 100);
    _captiveDetectHandles[_captiveDetectCount++] = _dispatcher->onGet("/ncsi.txt", redirect, 100);
    _captiveDetectHandles[_captiveDetectCount++] = _dispatcher->onGet("/mobile/status.php", redirect, 100);
    _captiveDetectHandles[_captiveDetectCount++] = _dispatcher->onGet("/canonical.html", redirect, 100);
    _captiveDetectHandles[_captiveDetectCount++] = _dispatcher->onGet("/success.txt", redirect, 100);

    LOG_INFO(TAG, "Captive portal ready with %d detection endpoints", _captiveDetectCount + 1);
}

void WiFiManager::teardownCaptivePortal() {
    if (!_dispatcher) return;

    if (_captiveRootHandle.valid()) {
        _dispatcher->off(_captiveRootHandle);
        _captiveRootHandle = HttpDispatcher::RouteHandle::invalid();
    }

    for (int i = 0; i < _captiveDetectCount; i++) {
        if (_captiveDetectHandles[i].valid()) {
            _dispatcher->off(_captiveDetectHandles[i]);
            _captiveDetectHandles[i] = HttpDispatcher::RouteHandle::invalid();
        }
    }
    _captiveDetectCount = 0;
}

} // namespace WiFiMan
