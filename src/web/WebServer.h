#ifndef SMARTGARLAND_WEBSERVER_H
#define SMARTGARLAND_WEBSERVER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <WiFiMan.h>

class WebServer {
public:
    static void init(AsyncWebServer& server, WiFiMan::WiFiManager& wifiMan);
    static void begin(AsyncWebServer& server);

private:
    static String templateProcessor(const String& var);
};

#endif // SMARTGARLAND_WEBSERVER_H
