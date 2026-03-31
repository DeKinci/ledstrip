#include "HttpServer.h"

HttpServer::HttpServer(uint16_t port)
    : _server(port)
    , _dispatcher(httpDispatcher)
    , _port(port)
{
}

HttpServer::HttpServer(uint16_t port, HttpDispatcher& dispatcher)
    : _server(port)
    , _dispatcher(dispatcher)
    , _port(port)
{
}

void HttpServer::begin() {
    _server.begin();
}

void HttpServer::loop() {
    WiFiClient client = _server.accept();
    if (!client) {
        return;
    }

    client.setNoDelay(true);

    HttpRequest req;
    if (!HttpRequestReader::read(client, _buffer, req)) {
        return;  // Reader already sent error response
    }

    HttpResponse res = _dispatcher.dispatch(req);

    HttpResponseWriter::write(client, res);
    client.stop();
}