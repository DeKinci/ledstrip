#include "HttpServer.h"

HttpServer::HttpServer(uint16_t port)
    : _server(port)
    , _dispatcher(httpDispatcher)
    , _port(port)
{
    registerDefaults();
}

HttpServer::HttpServer(uint16_t port, HttpDispatcher& dispatcher)
    : _server(port)
    , _dispatcher(dispatcher)
    , _port(port)
{
    registerDefaults();
}

void HttpServer::registerDefaults() {
    // Low priority so user routes override
    _dispatcher.onGet("/ping", [](HttpRequest&, ResponseBuffer&) {
        return HttpResponse::text("pong");
    }, -1);
}

void HttpServer::begin() {
    _server.begin();
}

void HttpServer::loop() {
    WiFiClient client = _server.accept();
    if (!client) return;

    client.setNoDelay(true);
    _responseBuf.reset();

    HttpRequest req;
    if (!HttpRequestReader::read(client, _requestBuf, req, _config.reader)) {
        return;
    }

    HttpResponse res = _dispatcher.dispatch(req, _responseBuf);
    HttpResponseWriter::write(client, res);
    client.stop();
}
