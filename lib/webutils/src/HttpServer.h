#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <WiFi.h>
#include "HttpDispatcher.h"
#include "HttpDispatcherDefault.h"
#include "HttpRequest.h"
#include "HttpRequestReader.h"
#include "HttpResponse.h"
#include "HttpResponseWriter.h"
#include "RequestBuffer.h"

class HttpServer {
public:
    // Use global httpDispatcher by default
    explicit HttpServer(uint16_t port = 80);

    // Use custom dispatcher
    HttpServer(uint16_t port, HttpDispatcher& dispatcher);

    // Start the server
    void begin();

    // Process incoming connections - call in loop()
    void loop();

    // Access dispatcher for route registration
    HttpDispatcher& dispatcher() { return _dispatcher; }
    const HttpDispatcher& dispatcher() const { return _dispatcher; }

    // Get port
    uint16_t port() const { return _port; }

private:
    WiFiServer _server;
    HttpDispatcher& _dispatcher;
    RequestBuffer _buffer;  // One buffer per server
    uint16_t _port;
};

#endif