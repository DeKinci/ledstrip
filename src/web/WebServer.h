#ifndef SMARTGARLAND_WEBSERVER_H
#define SMARTGARLAND_WEBSERVER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <NetWizard.h>

class WebServer {
public:
    static void init(AsyncWebServer& server, NetWizard& nw);
    static void begin(AsyncWebServer& server);

private:
    static String templateProcessor(const String& var);
};

#endif // SMARTGARLAND_WEBSERVER_H
