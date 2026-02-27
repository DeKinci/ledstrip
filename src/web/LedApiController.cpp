#include "LedApiController.h"

#include <ArduinoJson.h>
#include <HttpRequest.h>
#include <HttpResponse.h>

#include "ble/BleDeviceManager.hpp"

namespace LedApiController {

void registerRoutes(HttpDispatcher& dispatcher) {
    // Shader routes removed - now handled via MicroProto RESOURCE operations

    // ============== BLE Routes ==============

    // POST /api/ble/scan - Trigger BLE scan
    dispatcher.onPost("/api/ble/scan", [](HttpRequest& req) {
        BleDeviceManager::triggerScanNow();
        return HttpResponse::json("{\"success\":true,\"message\":\"Scan started\"}");
    });

    // GET /api/ble/scan/results - Get scan results
    dispatcher.onGet("/api/ble/scan/results", [](HttpRequest& req) {
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
        return HttpResponse::json(doc);
    });

    // GET /api/ble/known - List known devices
    dispatcher.onGet("/api/ble/known", [](HttpRequest& req) {
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
        return HttpResponse::json(doc);
    });

    // POST /api/ble/known - Add known device
    dispatcher.onPost("/api/ble/known", [](HttpRequest& req) {
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

    // DELETE /api/ble/known/{addr} - Remove known device
    dispatcher.onDelete("/api/ble/known/{addr}", [](HttpRequest& req) {
        String address = req.pathParam("addr").toString();

        if (BleDeviceManager::removeKnownDevice(address.c_str())) {
            return HttpResponse::json("{\"success\":true}");
        } else {
            return HttpResponse::json("{\"error\":\"Device not found\"}", 404);
        }
    });

    // GET /api/ble/connected - List connected devices
    dispatcher.onGet("/api/ble/connected", [](HttpRequest& req) {
        JsonDocument doc;
        JsonArray devices = doc["devices"].to<JsonArray>();

        for (const auto& conn : BleDeviceManager::getConnectedDevices()) {
            if (!conn.valid) continue;
            JsonObject obj = devices.add<JsonObject>();
            obj["address"] = conn.device.address;
            obj["name"] = conn.device.name;
            obj["icon"] = conn.device.icon;
        }
        return HttpResponse::json(doc);
    });

    // POST /api/ble/connect/{addr} - Connect to device
    dispatcher.onPost("/api/ble/connect/{addr}", [](HttpRequest& req) {
        String address = req.pathParam("addr").toString();

        if (BleDeviceManager::connectToDevice(address.c_str())) {
            return HttpResponse::json("{\"success\":true}");
        } else {
            return HttpResponse::json("{\"error\":\"Failed to connect\"}", 500);
        }
    });

    // POST /api/ble/disconnect/{addr} - Disconnect from device
    dispatcher.onPost("/api/ble/disconnect/{addr}", [](HttpRequest& req) {
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