#include "LedApiController.h"

#include <ArduinoJson.h>
#include <HttpRequest.h>
#include <HttpResponse.h>
#include <ResponseBuffer.h>

#include "ble/BleDeviceManager.hpp"

// Helper: serialize ArduinoJson doc into ResponseBuffer, return json HttpResponse
static HttpResponse jsonResponse(const JsonDocument& doc, ResponseBuffer& buf, int code = 200) {
    char* start = buf.writePtr();
    size_t len = serializeJson(doc, start, buf.remaining());
    buf.advance(len);
    return HttpResponse::json(start, len, code);
}

namespace LedApiController {

void registerRoutes(HttpDispatcher& dispatcher) {
    // POST /api/ble/scan
    dispatcher.onPost("/api/ble/scan", [](HttpRequest& req, ResponseBuffer&) {
        BleDeviceManager::triggerScanNow();
        return HttpResponse::json("{\"success\":true,\"message\":\"Scan started\"}");
    });

    // GET /api/ble/scan/results
    dispatcher.onGet("/api/ble/scan/results", [](HttpRequest& req, ResponseBuffer& buf) {
        bool scanning = BleDeviceManager::isScanning();
        const auto& results = BleDeviceManager::getLastScanResults();
        size_t count = BleDeviceManager::getLastScanResultCount();
        Serial.printf("[API] scan/results: scanning=%d, devices=%d\n", scanning, count);

        JsonDocument doc;
        doc["scanning"] = scanning;
        JsonArray devices = doc["devices"].to<JsonArray>();

        for (const auto& device : results) {
            if (!device.valid) continue;
            JsonObject obj = devices.add<JsonObject>();
            obj["address"] = device.address;
            obj["name"] = device.name;
            obj["icon"] = device.icon;
            obj["lastSeen"] = device.lastSeen;
        }
        return jsonResponse(doc, buf);
    });

    // GET /api/ble/known
    dispatcher.onGet("/api/ble/known", [](HttpRequest& req, ResponseBuffer& buf) {
        JsonDocument doc;
        JsonArray devices = doc["devices"].to<JsonArray>();

        for (const auto& device : BleDeviceManager::getKnownDevices()) {
            if (!device.valid) continue;
            JsonObject obj = devices.add<JsonObject>();
            obj["address"] = device.address;
            obj["name"] = device.name;
            obj["icon"] = device.icon;
            obj["autoConnect"] = device.autoConnect;
        }
        return jsonResponse(doc, buf);
    });

    // POST /api/ble/known
    dispatcher.onPost("/api/ble/known", [](HttpRequest& req, ResponseBuffer&) {
        JsonDocument doc;
        if (!req.json(doc)) {
            return HttpResponse::json("{\"error\":\"Invalid JSON\"}", 400);
        }

        const char* address = doc["address"] | "";
        const char* name = doc["name"] | "";
        const char* icon = doc["icon"] | "generic";
        bool autoConnect = doc["autoConnect"] | true;

        if (address[0] == '\0') {
            return HttpResponse::json("{\"error\":\"Missing address\"}", 400);
        }

        if (BleDeviceManager::addKnownDevice(address, name, icon, autoConnect)) {
            return HttpResponse::json("{\"success\":true}");
        } else {
            return HttpResponse::json("{\"error\":\"Failed to add device\"}", 500);
        }
    });

    // DELETE /api/ble/known/{addr}
    dispatcher.onDelete("/api/ble/known/{addr}", [](HttpRequest& req, ResponseBuffer&) {
        String address = req.pathParam("addr").toString();

        if (BleDeviceManager::removeKnownDevice(address.c_str())) {
            return HttpResponse::json("{\"success\":true}");
        } else {
            return HttpResponse::json("{\"error\":\"Device not found\"}", 404);
        }
    });

    // GET /api/ble/connected
    dispatcher.onGet("/api/ble/connected", [](HttpRequest& req, ResponseBuffer& buf) {
        JsonDocument doc;
        JsonArray devices = doc["devices"].to<JsonArray>();

        for (const auto& conn : BleDeviceManager::getConnectedDevices()) {
            if (!conn.valid) continue;
            JsonObject obj = devices.add<JsonObject>();
            obj["address"] = conn.device.address;
            obj["name"] = conn.device.name;
            obj["icon"] = conn.device.icon;
        }
        return jsonResponse(doc, buf);
    });

    // POST /api/ble/connect/{addr}
    dispatcher.onPost("/api/ble/connect/{addr}", [](HttpRequest& req, ResponseBuffer&) {
        String address = req.pathParam("addr").toString();

        if (BleDeviceManager::connectToDevice(address.c_str())) {
            return HttpResponse::json("{\"success\":true}");
        } else {
            return HttpResponse::json("{\"error\":\"Failed to connect\"}", 500);
        }
    });

    // POST /api/ble/disconnect/{addr}
    dispatcher.onPost("/api/ble/disconnect/{addr}", [](HttpRequest& req, ResponseBuffer&) {
        String address = req.pathParam("addr").toString();

        if (BleDeviceManager::disconnectDevice(address.c_str())) {
            return HttpResponse::json("{\"success\":true}");
        } else {
            return HttpResponse::json("{\"error\":\"Device not connected\"}", 404);
        }
    });

    Serial.println("[LedApiController] BLE routes registered");
}

}  // namespace LedApiController
