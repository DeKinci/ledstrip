#include "HttpResponse.h"

HttpResponse::HttpResponse()
    : _statusCode(200),
      _contentType("text/plain"),
      _bodyLength(0) {}

HttpResponse& HttpResponse::status(int code) {
    _statusCode = code;
    return *this;
}

HttpResponse& HttpResponse::contentType(const String& type) {
    _contentType = type;
    return *this;
}

HttpResponse& HttpResponse::header(const String& name, const String& value) {
    _headers += name + ": " + value + "\r\n";
    return *this;
}

HttpResponse& HttpResponse::body(const String& content) {
    _bodyString = content;
    _bodyRef = nullptr;
    _bodyLength = content.length();
    return *this;
}

HttpResponse& HttpResponse::body(const uint8_t* data, size_t len) {
    _bodyString = "";
    _bodyRef = data;
    _bodyLength = len;
    return *this;
}

HttpResponse HttpResponse::text(const String& content, int code) {
    return HttpResponse().status(code).contentType("text/plain").body(content);
}

HttpResponse HttpResponse::json(const String& content, int code) {
    return HttpResponse().status(code).contentType("application/json").body(content);
}

HttpResponse HttpResponse::html(const String& content, int code) {
    return HttpResponse().status(code).contentType("text/html").body(content);
}

HttpResponse HttpResponse::html(const uint8_t* data, size_t len, int code) {
    return HttpResponse().status(code).contentType("text/html").body(data, len);
}

HttpResponse HttpResponse::notFound(const String& message) {
    return text(message, 404);
}

HttpResponse HttpResponse::badRequest(const String& message) {
    return text(message, 400);
}

HttpResponse HttpResponse::error(const String& message) {
    return text(message, 500);
}

HttpResponse HttpResponse::withStatus(int code, const String& message) {
    if (message.length() > 0) {
        return text(message, code);
    }
    return HttpResponse().status(code);
}