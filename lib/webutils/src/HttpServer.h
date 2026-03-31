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
#include "ResponseBuffer.h"

struct HttpServerConfig {
    HttpReaderConfig reader;
};

class HttpServer {
public:
    explicit HttpServer(uint16_t port = 80);
    HttpServer(uint16_t port, HttpDispatcher& dispatcher);

    void begin();
    void loop();

    HttpDispatcher& dispatcher() { return _dispatcher; }
    const HttpDispatcher& dispatcher() const { return _dispatcher; }

    HttpServerConfig& config() { return _config; }
    uint16_t port() const { return _port; }
    size_t bufferCapacity() const { return _requestBuf.capacity(); }
    ResponseBuffer& responseBuffer() { return _responseBuf; }

private:
    WiFiServer _server;
    HttpDispatcher& _dispatcher;
    RequestBuffer _requestBuf;
    ResponseBuffer _responseBuf;
    uint16_t _port;
    HttpServerConfig _config;
};

#endif
