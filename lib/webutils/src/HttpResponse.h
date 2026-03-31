#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include <Arduino.h>
#include <ArduinoJson.h>

class HttpResponse {
   public:
    HttpResponse();

    // Builder methods (return *this for chaining)
    HttpResponse& status(int code);
    HttpResponse& contentType(const String& type);
    HttpResponse& header(const String& name, const String& value);
    HttpResponse& body(const String& content);
    HttpResponse& body(const uint8_t* data, size_t len);  // Non-owning, data must outlive response

    // Convenience builders
    static HttpResponse ok() { return withStatus(200); }
    static HttpResponse text(const String& content, int code = 200);
    static HttpResponse json(const String& content, int code = 200);
    static HttpResponse html(const String& content, int code = 200);
    static HttpResponse html(const uint8_t* data, size_t len, int code = 200);
    static HttpResponse notFound(const String& message = "Not Found");
    static HttpResponse badRequest(const String& message = "Bad Request");
    static HttpResponse error(const String& message = "Internal Server Error");
    static HttpResponse withStatus(int code, const String& message = "");

    // JSON from ArduinoJson document (only matches types with .size() method, excluding const char*)
    template<typename JsonDocType,
             typename = decltype(std::declval<JsonDocType>().size())>
    static HttpResponse json(const JsonDocType& doc, int code = 200) {
        String content;
        serializeJson(doc, content);
        return json(content, code);
    }

    // Getters
    int statusCode() const { return _statusCode; }
    const String& contentTypeValue() const { return _contentType; }
    const String& headers() const { return _headers; }
    const String& bodyString() const { return _bodyString; }
    const uint8_t* bodyData() const { return _bodyRef; }
    size_t bodyLength() const { return _bodyLength; }
    bool hasBinaryBody() const { return _bodyRef != nullptr; }

   private:
    int _statusCode;
    String _contentType;
    String _headers;
    String _bodyString;
    const uint8_t* _bodyRef = nullptr;  // Non-owning reference (e.g., PROGMEM)
    size_t _bodyLength;
};

#endif  // HTTP_RESPONSE_H