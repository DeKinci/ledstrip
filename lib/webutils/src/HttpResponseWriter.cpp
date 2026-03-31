#include "HttpResponseWriter.h"

void HttpResponseWriter::write(WiFiClient& client, const HttpResponse& response) {
    // Status line
    client.printf("HTTP/1.1 %d %s\r\n", response.statusCode(),
                  statusText(response.statusCode()));

    // Headers
    client.printf("Content-Type: %s\r\n", response.contentTypeValue().c_str());
    client.printf("Content-Length: %d\r\n", response.bodyLength());

    if (response.headers().length() > 0) {
        client.print(response.headers());
    }

    client.print("Connection: close\r\n\r\n");

    // Body
    if (response.hasBinaryBody()) {
        if (response.bodyData() && response.bodyLength() > 0) {
            client.write(response.bodyData(), response.bodyLength());
        }
    } else {
        if (response.bodyString().length() > 0) {
            client.print(response.bodyString());
        }
    }
}

const char* HttpResponseWriter::statusText(int code) {
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 408: return "Request Timeout";
        case 500: return "Internal Server Error";
        default: return "Unknown";
    }
}