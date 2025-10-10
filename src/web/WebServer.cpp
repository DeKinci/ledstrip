#include "WebServer.h"
#include "ApiController.h"
#include "SocketController.h"
#include "w_index_html.h"
#include "core/ShaderStorage.h"

#include <WiFi.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>

void WebServer::init(AsyncWebServer& server, WiFiMan::WiFiManager& wifiMan) {
    // Configure WiFiMan BEFORE server.reset() so routes are registered
    wifiMan.setAPCredentials("SmartGarland", "");
    wifiMan.setHostname("led");

    // Setup callbacks
    wifiMan.onConnected([](const String& ssid) {
        Serial.printf("\n✓ WiFi connected to: %s\n", ssid.c_str());
        Serial.printf("  IP: %s\n", WiFi.localIP().toString().c_str());

        // Start mDNS
        if (MDNS.begin("led")) {
            MDNS.addService("http", "tcp", 80);
            Serial.println("  mDNS: http://led.local/");
        }
    });

    wifiMan.onDisconnected([]() {
        Serial.println("✗ WiFi disconnected");
    });

    wifiMan.onAPStarted([](const String& ssid) {
        Serial.printf("\n⚠ AP Mode: %s\n", ssid.c_str());
        Serial.printf("  Connect and visit: http://%s/\n", WiFi.softAPIP().toString().c_str());
    });

    // Start WiFi manager (non-blocking) - this sets up web routes
    wifiMan.begin();

    // CORS headers
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS, PUT, DELETE");

    // Basic routes
    server.on("/ping", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send_P(200, "text/plain", "pong");
    });

    server.on("/nuke", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("NUKE TRIGGERED: formatting SPIFFS...");
        delay(100); // allow response to go through
        ShaderStorage::get().nuke();
        request->send(200, "text/plain", "Rebooting and formatting FS...");
    });

    // Main app UI at /index (not at / to avoid conflict with WiFiMan captive portal)
    server.on("/index", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send_P(200, "text/html", index_html, templateProcessor);
    });

    // API routes
    auto shaderPost = new AsyncCallbackJsonWebHandler("/api/shader", [](AsyncWebServerRequest *request, JsonVariant &json) {
        ApiController::onAddShader(request, json);
    });
    shaderPost->setMethod(HTTP_POST);
    server.addHandler(shaderPost);

    server.on("^\\/api\\/show\\/([a-zA-Z0-9_-]+)$", HTTP_GET, [](AsyncWebServerRequest *request) {
        String path = request->pathArg(0);
        ApiController::onShow(path, request);
    });

    server.on("^\\/api\\/shader\\/([a-zA-Z0-9_-]+)$", HTTP_GET, [](AsyncWebServerRequest *request) {
        String path = request->pathArg(0);
        ApiController::onGetShader(path, request);
    });

    server.on("^\\/api\\/shader\\/([a-zA-Z0-9_-]+)$", HTTP_DELETE, [](AsyncWebServerRequest *request) {
        String path = request->pathArg(0);
        ApiController::onDeleteShader(path, request);
    });

    server.on("/api/shader", HTTP_GET, [](AsyncWebServerRequest *request) {
        ApiController::onListShaders(request);
    });

    server.on("/api/show", HTTP_GET, [](AsyncWebServerRequest *request) {
        ApiController::onGetShow(request);
    });

    // Bind WebSocket
    SocketController::bind(server);
}

void WebServer::begin(AsyncWebServer& server) {
    server.begin();
    Serial.println("http://led.local/");
}

String WebServer::templateProcessor(const String& var) {
    if (var == "SELF_IP") {
        return String(WiFi.localIP().toString());
    }
    return String();
}
