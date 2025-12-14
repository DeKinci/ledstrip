#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <Arduino.h>

class HttpRequest {
   public:
    HttpRequest();

    // Parse raw HTTP request string
    bool parse(const String& rawRequest);

    // Getters
    const String& method() const { return _method; }
    const String& path() const { return _path; }
    const String& version() const { return _version; }
    const String& body() const { return _body; }

    // Header access
    String header(const String& name) const;
    bool hasHeader(const String& name) const;

    // Path helpers
    bool pathStartsWith(const String& prefix) const;
    String pathSuffix(const String& prefix) const;

    // Query parameters
    String queryParam(const String& name) const;
    bool hasQueryParam(const String& name) const;

    // Validity
    bool isValid() const { return _valid; }

    // Debug
    String toString() const;

   private:
    bool _valid;
    String _method;
    String _path;
    String _version;
    String _body;
    String _rawHeaders;

    void reset();
    bool parseRequestLine(const String& line);
    void parseHeaders(const String& headers);
};

#endif  // HTTP_REQUEST_H
