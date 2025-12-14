#ifndef API_CONTROLLER_H
#define API_CONTROLLER_H

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>

#include "animations/Anime.h"
#include "core/ShaderStorage.h"

namespace ApiController {
// Shader endpoints
void onAddShader(WiFiClient& client, const String& body);
void onListShaders(WiFiClient& client);
void onGetShader(WiFiClient& client, const String& shader);
void onDeleteShader(WiFiClient& client, const String& shader);

void onShow(WiFiClient& client, const String& shader);
void onGetShow(WiFiClient& client);

// BLE endpoints
void onBleScan(WiFiClient& client);
void onBleGetScanResults(WiFiClient& client);
void onBleGetKnownDevices(WiFiClient& client);
void onBleAddDevice(WiFiClient& client, const String& body);
void onBleRemoveDevice(WiFiClient& client, const String& address);
void onBleGetConnectedDevices(WiFiClient& client);
void onBleConnect(WiFiClient& client, const String& address);
void onBleDisconnect(WiFiClient& client, const String& address);

// Helper
void sendJson(WiFiClient& client, int code, const JsonDocument& doc);
void sendError(WiFiClient& client, int code, const String& message);
};  // namespace ApiController

#endif  // API_CONTROLLER_H