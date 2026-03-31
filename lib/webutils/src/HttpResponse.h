#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include <cstddef>
#include <cstdint>
#include "ResponseBuffer.h"
#include "Resource.h"

class HttpResponse {
public:
    HttpResponse() = default;

    // --- Builder methods (return *this for chaining) ---

    HttpResponse& status(int code) { _statusCode = code; return *this; }
    HttpResponse& contentType(const char* type) { _contentType = type; return *this; }
    HttpResponse& encoding(ResourceEncoding enc) { _encoding = enc; return *this; }
    HttpResponse& etag(const char* tag) { _etag = tag; return *this; }
    // Body: string literal (zero-copy, most common)
    HttpResponse& body(const char* literal) {
        _bodyData = literal;
        _bodyBinary = nullptr;
        _bodyLength = literal ? __builtin_strlen(literal) : 0;
        return *this;
    }

    // Body: pointer + length (for non-literal strings, e.g. ResponseBuffer data)
    HttpResponse& body(const char* data, size_t len) {
        _bodyData = data;
        _bodyBinary = nullptr;
        _bodyLength = len;
        return *this;
    }

    // Body: binary PROGMEM data (zero-copy)
    HttpResponse& body(const uint8_t* data, size_t len) {
        _bodyBinary = data;
        _bodyData = nullptr;
        _bodyLength = len;
        return *this;
    }

    // Custom header — writes into ResponseBuffer (rare)
    HttpResponse& header(const char* name, const char* value, ResponseBuffer& buf) {
        if (!_customHeaders) _customHeaders = buf.writePtr();
        buf.printf("%s: %s\r\n", name, value);
        _customHeadersLen = buf.writePtr() - _customHeaders;
        return *this;
    }

    // --- Static factories ---

    static HttpResponse text(const char* content, int code = 200) {
        return HttpResponse().status(code).contentType("text/plain").body(content);
    }
    static HttpResponse text(const char* data, size_t len, int code = 200) {
        return HttpResponse().status(code).contentType("text/plain").body(data, len);
    }
    static HttpResponse json(const char* content, int code = 200) {
        return HttpResponse().status(code).contentType("application/json").body(content);
    }
    static HttpResponse json(const char* data, size_t len, int code = 200) {
        return HttpResponse().status(code).contentType("application/json").body(data, len);
    }
    static HttpResponse html(const char* content, int code = 200) {
        return HttpResponse().status(code).contentType("text/html").body(content);
    }
    static HttpResponse html(const uint8_t* data, size_t len, int code = 200) {
        return HttpResponse().status(code).contentType("text/html").body(data, len);
    }
    static HttpResponse brotli(const uint8_t* data, size_t len, const char* contentType) {
        return HttpResponse().status(200).contentType(contentType).body(data, len).encoding(ResourceEncoding::BROTLI);
    }
    static HttpResponse gzip(const uint8_t* data, size_t len, const char* contentType) {
        return HttpResponse().status(200).contentType(contentType).body(data, len).encoding(ResourceEncoding::GZIP);
    }
    static HttpResponse notFound(const char* message = "Not Found") {
        return text(message, 404);
    }
    static HttpResponse badRequest(const char* message = "Bad Request") {
        return text(message, 400);
    }
    static HttpResponse error(const char* message = "Internal Server Error") {
        return text(message, 500);
    }
    static HttpResponse ok() { return HttpResponse().status(200); }
    static HttpResponse withStatus(int code, const char* message = nullptr) {
        if (message && message[0]) return text(message, code);
        return HttpResponse().status(code);
    }
    static HttpResponse notModified(const char* etagValue) {
        return HttpResponse().status(304).etag(etagValue);
    }

    // --- Getters ---

    int statusCode() const { return _statusCode; }
    const char* contentTypeValue() const { return _contentType; }
    ResourceEncoding encodingValue() const { return _encoding; }
    const char* etagValue() const { return _etag; }

    // Body accessors
    bool hasBinaryBody() const { return _bodyBinary != nullptr; }
    const uint8_t* bodyBinary() const { return _bodyBinary; }
    const char* bodyData() const { return _bodyData; }
    size_t bodyLength() const { return _bodyLength; }

    // Custom headers
    const char* customHeaders() const { return _customHeaders; }
    size_t customHeadersLen() const { return _customHeadersLen; }

private:
    int _statusCode = 200;
    const char* _contentType = "text/plain";
    ResourceEncoding _encoding = ResourceEncoding::NONE;
    const char* _etag = nullptr;

    const char* _bodyData = nullptr;           // String body (literal or buffer pointer)
    const uint8_t* _bodyBinary = nullptr;      // Binary/PROGMEM body
    size_t _bodyLength = 0;

    const char* _customHeaders = nullptr;      // Points into ResponseBuffer
    size_t _customHeadersLen = 0;
};

#endif // HTTP_RESPONSE_H
