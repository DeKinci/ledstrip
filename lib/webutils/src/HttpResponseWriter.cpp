#include "HttpResponseWriter.h"

static const char CORS_HEADERS[] =
    "Access-Control-Allow-Origin: *\r\n"
    "Access-Control-Allow-Methods: *\r\n"
    "Access-Control-Allow-Headers: *\r\n"
    "Access-Control-Max-Age: 86400\r\n";

static const char* encodingStr(ResourceEncoding enc) {
    switch (enc) {
        case ResourceEncoding::BROTLI: return "br";
        case ResourceEncoding::GZIP: return "gzip";
        default: return nullptr;
    }
}

void HttpResponseWriter::write(WiFiClient& client, const HttpResponse& response) {
    // Status line
    client.printf("HTTP/1.1 %d %s\r\n", response.statusCode(),
                  statusText(response.statusCode()));

    // Content headers (skip for 304)
    if (response.statusCode() != 304) {
        client.printf("Content-Type: %s\r\n", response.contentTypeValue());
        client.printf("Content-Length: %d\r\n", response.bodyLength());
    }

    // Content-Encoding
    const char* enc = encodingStr(response.encodingValue());
    if (enc) client.printf("Content-Encoding: %s\r\n", enc);

    // ETag + Cache-Control
    if (response.etagValue()) {
        client.printf("ETag: \"%s\"\r\nCache-Control: no-cache\r\n", response.etagValue());
    }

    // CORS (always)
    client.print(CORS_HEADERS);

    // Custom headers (rare)
    if (response.customHeaders() && response.customHeadersLen() > 0) {
        client.write(response.customHeaders(), response.customHeadersLen());
    }

    // End of headers
    client.print("\r\n");

    // Body
    if (response.hasBinaryBody()) {
        if (response.bodyBinary() && response.bodyLength() > 0) {
            client.write(response.bodyBinary(), response.bodyLength());
        }
    } else if (response.bodyData() && response.bodyLength() > 0) {
        client.write(response.bodyData(), response.bodyLength());
    }
}

const char* HttpResponseWriter::statusText(int code) {
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 408: return "Request Timeout";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        default: return "Unknown";
    }
}
