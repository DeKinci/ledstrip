#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <StringView.h>

class HttpRequest {
   public:
    HttpRequest();

    // Factory for invalid request
    static HttpRequest invalid() { return HttpRequest(); }

    // Parse raw HTTP request from buffer (data must outlive HttpRequest)
    bool parse(const char* data, size_t len);

    // Getters (zero-copy views into buffer)
    StringView method() const { return _method; }
    StringView path() const { return _path; }
    StringView version() const { return _version; }
    StringView body() const { return _body; }

    // Header access
    StringView header(StringView name) const;
    bool hasHeader(StringView name) const;

    // Path helpers
    bool pathStartsWith(StringView prefix) const;
    StringView pathSuffix(StringView prefix) const;

    // Query parameters
    StringView queryParam(StringView name) const;
    bool hasQueryParam(StringView name) const;

    // Route matching - pattern like "/api/v1/user/{userid}/avatar"
    bool match(StringView method, StringView pattern);
    bool matchGet(StringView pattern) { return match("GET", pattern); }
    bool matchPost(StringView pattern) { return match("POST", pattern); }
    bool matchPut(StringView pattern) { return match("PUT", pattern); }
    bool matchDelete(StringView pattern) { return match("DELETE", pattern); }
    StringView pathParam(StringView name) const;

    // Validity
    explicit operator bool() const { return _valid; }

    // JSON parsing - returns true if body was successfully parsed as JSON
    template<typename JsonDocType>
    bool json(JsonDocType& doc) const {
        if (_body.empty()) return false;
        auto err = deserializeJson(doc, _body.data(), _body.length());
        return !err;
    }

    // Debug (allocates String)
    String toString() const;

   private:
    static const int MAX_PATH_PARAMS = 4;
    static const int MAX_QUERY_PARAMS = 4;

    bool _valid;
    StringView _method;
    StringView _path;
    StringView _version;
    StringView _body;
    StringView _rawHeaders;

    // Path parameter storage (names from pattern, values from path)
    StringView _pathParamNames[MAX_PATH_PARAMS];
    StringView _pathParamValues[MAX_PATH_PARAMS];
    int _pathParamCount = 0;

    // Query parameter storage (both point into buffer)
    StringView _queryParamNames[MAX_QUERY_PARAMS];
    StringView _queryParamValues[MAX_QUERY_PARAMS];
    int _queryParamCount = 0;

    void reset();
    bool parseRequestLine(StringView line);
    void clearPathParams();
    void parseQueryString(StringView queryString);
};

#endif  // HTTP_REQUEST_H