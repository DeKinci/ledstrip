// Test firmware for WiFiMan integration tests.
// /ping is provided by HttpServer automatically.

#include <Arduino.h>
#include <WiFi.h>
#include <HttpServer.h>
#include <WiFiMan.h>

static HttpServer http(80);
static WiFiMan::WiFiManager wifiManager(&http.dispatcher());

void setup() {
    Serial.begin(115200);
    Serial.println("test_wifi_api: starting");

    wifiManager.begin();
    http.begin();

    Serial.println("test_wifi_api: ready");
}

void loop() {
    wifiManager.loop();
    http.loop();
}
