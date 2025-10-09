#include "WebServer.h"
#include "ApiController.h"
#include "SocketController.h"
#include "w_index_html.h"
#include "core/ShaderStorage.h"

#include <WiFi.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>

void WebServer::init(AsyncWebServer& server, NetWizard& nw) {
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);

    server.reset();

    nw.setStrategy(NetWizardStrategy::NON_BLOCKING);
    nw.autoConnect("LED", "");
    Serial.print("Connected to wifi: ");
    Serial.println(WiFi.localIP());

    if (!MDNS.begin("led")) {
        Serial.println("Error starting mDNS");
        return;
    }
    MDNS.addService("http", "tcp", 80);

    // CORS headers
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS, PUT, DELETE");

    // 404 handler with OPTIONS support
    server.onNotFound([](AsyncWebServerRequest *request) {
        if (request->method() == HTTP_OPTIONS) {
            request->send(200);
        } else {
            request->send(404);
        }
    });

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

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
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
