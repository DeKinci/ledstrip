#include "ApiController.h"
#include "SocketController.h"
#include "ble/BleDeviceManager.hpp"

namespace ApiController {

void sendJson(WiFiClient& client, int code, const JsonDocument& doc) {
    String output;
    serializeJson(doc, output);

    client.printf("HTTP/1.1 %d OK\r\n", code);
    client.println("Content-Type: application/json");
    client.printf("Content-Length: %d\r\n", output.length());
    client.println("Connection: close");
    client.println();
    client.print(output);
}

void sendError(WiFiClient& client, int code, const String& message) {
    client.printf("HTTP/1.1 %d Error\r\n", code);
    client.println("Content-Type: text/plain");
    client.printf("Content-Length: %d\r\n", message.length());
    client.println("Connection: close");
    client.println();
    client.print(message);
}

// Shader endpoints

void onAddShader(WiFiClient& client, const String& body) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
        sendError(client, 400, "Invalid JSON");
        return;
    }

    String name = doc["name"].as<String>();
    String shader = doc["shader"].as<String>();

    Serial.printf("[API] onAddShader: %s (%d bytes)\n", name.c_str(), shader.length());

    CallResult<void *> storeResult = ShaderStorage::get().storeShader(name, shader);

    if (!storeResult.hasError()) {
        Anime::scheduleReload();
        SocketController::animationAdded(name);

        client.println("HTTP/1.1 200 OK");
        client.println("Content-Length: 0");
        client.println("Connection: close");
        client.println();
    } else {
        sendError(client, storeResult.getCode(), storeResult.getMessage());
    }
}

void onListShaders(WiFiClient& client) {
    CallResult<std::vector<String>> listResult = ShaderStorage::get().listShaders();
    if (listResult.hasError()) {
        sendError(client, listResult.getCode(), listResult.getMessage());
        return;
    }

    std::vector<String> list = listResult.getValue();

    JsonDocument doc;
    JsonArray names = doc["shader"].to<JsonArray>();
    for (const String& str : list) {
        names.add(str);
    }

    sendJson(client, 200, doc);
}

void onGetShader(WiFiClient& client, const String& shader) {
    CallResult<String> result = ShaderStorage::get().getShader(shader);
    if (result.hasError()) {
        sendError(client, result.getCode(), result.getMessage());
        return;
    }

    JsonDocument doc;
    doc["shader"] = result.getValue();

    sendJson(client, 200, doc);
}

void onDeleteShader(WiFiClient& client, const String& shader) {
    if (ShaderStorage::get().deleteShader(shader)) {
        Anime::scheduleReload();
        SocketController::animationRemoved(shader);

        client.println("HTTP/1.1 200 OK");
        client.println("Content-Length: 0");
        client.println("Connection: close");
        client.println();
    } else {
        sendError(client, 404, "Shader not found");
    }
}

void onShow(WiFiClient& client, const String& shader) {
    String shaderName = shader;  // Make mutable copy
    CallResult<void *> result = Anime::select(shaderName);
    if (result.hasError()) {
        sendError(client, result.getCode(), result.getMessage());
        return;
    }

    SocketController::animationSelected(shader);

    client.println("HTTP/1.1 200 OK");
    client.println("Content-Length: 0");
    client.println("Connection: close");
    client.println();
}

void onGetShow(WiFiClient& client) {
    JsonDocument doc;
    doc["name"] = Anime::getCurrent();
    doc["ledLimit"] = Anime::getCurrentLeds();

    sendJson(client, 200, doc);
}

// BLE endpoints

void onBleScan(WiFiClient& client) {
    if (BleDeviceManager::isScanning()) {
        sendError(client, 409, "Scan already in progress");
        return;
    }

    BleDeviceManager::triggerScanNow();  // Use triggerScanNow() instead of startScan(10)

    client.println("HTTP/1.1 200 OK");
    client.println("Content-Length: 0");
    client.println("Connection: close");
    client.println();
}

void onBleGetScanResults(WiFiClient& client) {
    auto results = BleDeviceManager::getLastScanResults();  // Use getLastScanResults()

    JsonDocument doc;
    JsonArray devices = doc["devices"].to<JsonArray>();

    for (const auto& device : results) {
        JsonObject obj = devices.add<JsonObject>();
        obj["address"] = device.address;
        obj["name"] = device.name;
        // Note: lastSeen instead of rssi
    }

    sendJson(client, 200, doc);
}

void onBleGetKnownDevices(WiFiClient& client) {
    auto devices = BleDeviceManager::getKnownDevices();

    JsonDocument doc;
    JsonArray arr = doc["devices"].to<JsonArray>();

    for (const auto& device : devices) {
        JsonObject obj = arr.add<JsonObject>();
        obj["address"] = device.address;
        obj["name"] = device.name;
    }

    sendJson(client, 200, doc);
}

void onBleAddDevice(WiFiClient& client, const String& body) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
        sendError(client, 400, "Invalid JSON");
        return;
    }

    String address = doc["address"].as<String>();
    String name = doc["name"].as<String>();

    BleDeviceManager::addKnownDevice(address, name);

    client.println("HTTP/1.1 200 OK");
    client.println("Content-Length: 0");
    client.println("Connection: close");
    client.println();
}

void onBleRemoveDevice(WiFiClient& client, const String& address) {
    BleDeviceManager::removeKnownDevice(address);

    client.println("HTTP/1.1 200 OK");
    client.println("Content-Length: 0");
    client.println("Connection: close");
    client.println();
}

void onBleGetConnectedDevices(WiFiClient& client) {
    auto devices = BleDeviceManager::getConnectedDevices();

    JsonDocument doc;
    JsonArray arr = doc["devices"].to<JsonArray>();

    for (const auto* device : devices) {  // Note: devices are pointers
        JsonObject obj = arr.add<JsonObject>();
        obj["address"] = device->device.address;  // Access via device member
        obj["name"] = device->device.name;
    }

    sendJson(client, 200, doc);
}

void onBleConnect(WiFiClient& client, const String& address) {
    bool success = BleDeviceManager::connectToDevice(address);  // Use connectToDevice()

    if (success) {
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Length: 0");
        client.println("Connection: close");
        client.println();
    } else {
        sendError(client, 500, "Failed to connect");
    }
}

void onBleDisconnect(WiFiClient& client, const String& address) {
    BleDeviceManager::disconnectDevice(address);  // Use disconnectDevice()

    client.println("HTTP/1.1 200 OK");
    client.println("Content-Length: 0");
    client.println("Connection: close");
    client.println();
}

};  // namespace ApiController
