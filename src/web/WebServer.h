#ifndef SMARTGARLAND_WEBSERVER_H
#define SMARTGARLAND_WEBSERVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsServer.h>

class WebServer {
public:
    static void init();
    static void begin(WiFiServer& server, WebSocketsServer& ws);
    static void loop(WiFiServer& server, WebSocketsServer& ws);

private:
    static void handleClient(WiFiClient& client, WebSocketsServer& ws);
    static void sendResponse(WiFiClient& client, int code, const String& contentType, const String& body);
    static void sendJson(WiFiClient& client, int code, const String& json);
    static void sendGzip(WiFiClient& client, int code, const String& contentType, const uint8_t* data, size_t len);
};

#endif // SMARTGARLAND_WEBSERVER_H
