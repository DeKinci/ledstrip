#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <NetWizard.h>
#include <NimBLEDevice.h>

#include "animations/Anime.h"
#include "web/ApiController.h"
#include "web/SocketController.h"
#include "web/w_index_html.h"
#include "ble/BleClient.hpp"

DNSServer dnsServer;
AsyncWebServer server(80);
NetWizard NW(&server);
// AsyncWiFiManager wm(&server, &dnsServer);

CallResult<void*> status(nullptr);

uint32_t loopTimestampMillis = 0;
uint32_t loopIteration = 0;

String processor(const String& var);

void setup() {
    delay(200);
    Serial.begin(115200);

    BleClient::init();
    BleClient::scan();

    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);

    server.reset();

    NW.setStrategy(NetWizardStrategy::NON_BLOCKING);
    NW.autoConnect("LED", "");
    Serial.print("Connected to wifi: ");
    Serial.println(WiFi.localIP());

    if (!MDNS.begin("led")) {
        Serial.println("Error starting mDNS");
        return;
    }
    MDNS.addService("http", "tcp", 80);

    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS, PUT, DELETE");

    server.onNotFound([](AsyncWebServerRequest *request) {
        if (request->method() == HTTP_OPTIONS) {
            request->send(200);
        } else {
            request->send(404);
        }
    });

    server.on("/ping", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send_P(200, "text/plain", "pong");
    });

    server.on("/nuke", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("NUKE TRIGGERED: formatting SPIFFS...");
        delay(100); // allow response to go through
        ShaderStorage::get().nuke();
        request->send(200, "text/plain", "Rebooting and formatting FS...");
    });

    server.on("/health", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send_P((int)status.getCode(), "text/plain", status.getMessage().c_str());
    });

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send_P(200, "text/html", index_html, processor);
    });

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

    ShaderStorage::init();
    SocketController::bind(server);

    status = Anime::connect();
    while (status.hasError()) {
        Serial.print("Error starting Anime: ");
        Serial.println(status.getMessage());
        delay(1000);
        status = Anime::connect();
    }

    server.begin();
    Serial.println("http://led.local/");
}

void loop() {
    loopTimestampMillis = millis();
    loopIteration++;

    BleClient::loop();

    NW.loop();

    SocketController::cleanUp();
    status = Anime::draw();
}

String processor(const String& var) {
    if (var == "SELF_IP") {
        return String(WiFi.localIP().toString());
    }
    return String();
}
