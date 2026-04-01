#include "BleMan.h"
#include "gen/w_ble_htm.h"
#include <ArduinoJson.h>
#include <MicroLog.h>
#include <ResponseBuffer.h>

static const char* TAG = "BleMan";

static HttpResponse jsonResponse(const JsonDocument& doc, ResponseBuffer& buf, int code = 200) {
    char* start = buf.writePtr();
    size_t len = serializeJson(doc, start, buf.remaining());
    buf.advance(len);
    return HttpResponse::json(start, len, code);
}

namespace BleMan {

void BleManager::setupRoutes() {
    if (!_dispatcher) {
        LOG_WARN(TAG, "No dispatcher, web interface disabled");
        return;
    }

    LOG_INFO(TAG, "Setting up /bleman routes");

    // GET /bleman/types — registered driver types
    _dispatcher->onGet("/bleman/types", [this](HttpRequest& req, ResponseBuffer& buf) -> HttpResponse {
        JsonDocument doc;
        JsonArray types = doc["types"].to<JsonArray>();
        for (const auto& reg : _driverTypes) {
            if (reg.valid) types.add(reg.type);
        }
        return jsonResponse(doc, buf);
    });

    // POST /bleman/scan
    _dispatcher->onPost("/bleman/scan", [this](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        triggerScanNow();
        return HttpResponse::json("{\"success\":true,\"message\":\"Scan started\"}");
    });

    // GET /bleman/scan/results
    _dispatcher->onGet("/bleman/scan/results", [this](HttpRequest& req, ResponseBuffer& buf) -> HttpResponse {
        JsonDocument doc;
        doc["scanning"] = isScanning();
        JsonArray devices = doc["devices"].to<JsonArray>();

        for (const auto& device : _lastScanResults) {
            if (!device.valid) continue;
            JsonObject obj = devices.add<JsonObject>();
            obj["address"] = device.address;
            obj["name"] = device.name;
            obj["icon"] = device.icon;
            obj["lastSeen"] = device.lastSeen;
        }
        return jsonResponse(doc, buf);
    });

    // GET /bleman/known
    _dispatcher->onGet("/bleman/known", [this](HttpRequest& req, ResponseBuffer& buf) -> HttpResponse {
        JsonDocument doc;
        JsonArray devices = doc["devices"].to<JsonArray>();

        for (const auto& device : _knownDevices) {
            if (!device.valid) continue;
            JsonObject obj = devices.add<JsonObject>();
            obj["address"] = device.address;
            obj["name"] = device.name;
            obj["icon"] = device.icon;
            obj["type"] = device.type;
            obj["autoConnect"] = device.autoConnect;
        }
        return jsonResponse(doc, buf);
    });

    // POST /bleman/known
    _dispatcher->onPost("/bleman/known", [this](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        JsonDocument doc;
        if (!req.json(doc)) {
            return HttpResponse::json("{\"error\":\"Invalid JSON\"}", 400);
        }

        const char* address = doc["address"] | "";
        const char* name = doc["name"] | "";
        const char* icon = doc["icon"] | "generic";
        const char* type = doc["type"] | "";
        bool autoConnect = doc["autoConnect"] | true;

        if (address[0] == '\0') {
            return HttpResponse::json("{\"error\":\"Missing address\"}", 400);
        }

        if (addKnownDevice(address, name, icon, type, autoConnect)) {
            return HttpResponse::json("{\"success\":true}");
        } else {
            return HttpResponse::json("{\"error\":\"Failed to add device\"}", 500);
        }
    });

    // DELETE /bleman/known/{addr}
    _dispatcher->onDelete("/bleman/known/{addr}", [this](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        String address = req.pathParam("addr").toString();
        if (removeKnownDevice(address.c_str())) {
            return HttpResponse::json("{\"success\":true}");
        } else {
            return HttpResponse::json("{\"error\":\"Device not found\"}", 404);
        }
    });

    // GET /bleman/connected
    _dispatcher->onGet("/bleman/connected", [this](HttpRequest& req, ResponseBuffer& buf) -> HttpResponse {
        JsonDocument doc;
        JsonArray devices = doc["devices"].to<JsonArray>();

        for (const auto& conn : _connectedDevices) {
            if (!conn.isConnected()) continue;
            JsonObject obj = devices.add<JsonObject>();
            obj["address"] = conn.device.address;
            obj["name"] = conn.device.name;
            obj["icon"] = conn.device.icon;
            obj["type"] = conn.device.type;
        }
        return jsonResponse(doc, buf);
    });

    // POST /bleman/connect/{addr}
    _dispatcher->onPost("/bleman/connect/{addr}", [this](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        String address = req.pathParam("addr").toString();
        if (connectToDevice(address.c_str())) {
            return HttpResponse::json("{\"success\":true}");
        } else {
            return HttpResponse::json("{\"error\":\"Failed to connect\"}", 500);
        }
    });

    // POST /bleman/disconnect/{addr}
    _dispatcher->onPost("/bleman/disconnect/{addr}", [this](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        String address = req.pathParam("addr").toString();
        if (disconnectDevice(address.c_str())) {
            return HttpResponse::json("{\"success\":true}");
        } else {
            return HttpResponse::json("{\"error\":\"Device not connected\"}", 404);
        }
    });

    // Web UI
    _dispatcher->resource("/bleman", ble_htm);

    LOG_INFO(TAG, "Web routes ready at /bleman");
}

} // namespace BleMan
