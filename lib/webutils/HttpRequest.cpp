#include "HttpRequest.h"

HttpRequest::HttpRequest() : _valid(false) {}

void HttpRequest::reset() {
    _valid = false;
    _method = "";
    _path = "";
    _version = "";
    _body = "";
    _rawHeaders = "";
}

bool HttpRequest::parse(const String& rawRequest) {
    reset();

    if (rawRequest.length() == 0) {
        return false;
    }

    // Find header/body separator
    int headerEnd = rawRequest.indexOf("\r\n\r\n");
    if (headerEnd == -1) {
        return false;
    }

    // Extract body
    _body = rawRequest.substring(headerEnd + 4);

    // Split headers section
    String headerSection = rawRequest.substring(0, headerEnd);

    // Find first line (request line)
    int firstLineEnd = headerSection.indexOf("\r\n");
    if (firstLineEnd == -1) {
        // Single line request (no additional headers)
        if (!parseRequestLine(headerSection)) {
            return false;
        }
    } else {
        String requestLine = headerSection.substring(0, firstLineEnd);
        if (!parseRequestLine(requestLine)) {
            return false;
        }

        // Store remaining headers
        _rawHeaders = headerSection.substring(firstLineEnd + 2);
    }

    _valid = true;
    return true;
}

bool HttpRequest::parseRequestLine(const String& line) {
    // Parse: METHOD PATH VERSION
    int firstSpace = line.indexOf(' ');
    if (firstSpace == -1) {
        return false;
    }

    int secondSpace = line.indexOf(' ', firstSpace + 1);
    if (secondSpace == -1) {
        return false;
    }

    _method = line.substring(0, firstSpace);
    _path = line.substring(firstSpace + 1, secondSpace);
    _version = line.substring(secondSpace + 1);

    // Remove query string from path for storage
    int queryStart = _path.indexOf('?');
    if (queryStart != -1) {
        _path = _path.substring(0, queryStart);
    }

    return true;
}

String HttpRequest::header(const String& name) const {
    if (_rawHeaders.length() == 0) {
        return "";
    }

    // Search for "Name: value\r\n"
    String searchPattern = name + ":";
    int headerStart = _rawHeaders.indexOf(searchPattern);

    // Try case-insensitive search
    if (headerStart == -1) {
        String lowerHeaders = _rawHeaders;
        lowerHeaders.toLowerCase();
        String lowerName = name;
        lowerName.toLowerCase();
        searchPattern = lowerName + ":";
        headerStart = lowerHeaders.indexOf(searchPattern);

        if (headerStart == -1) {
            return "";
        }
    }

    int valueStart = headerStart + searchPattern.length();

    // Skip whitespace after colon
    while (valueStart < _rawHeaders.length() &&
           (_rawHeaders[valueStart] == ' ' || _rawHeaders[valueStart] == '\t')) {
        valueStart++;
    }

    int valueEnd = _rawHeaders.indexOf("\r\n", valueStart);
    if (valueEnd == -1) {
        valueEnd = _rawHeaders.length();
    }

    return _rawHeaders.substring(valueStart, valueEnd);
}

bool HttpRequest::hasHeader(const String& name) const {
    return header(name).length() > 0;
}

bool HttpRequest::pathStartsWith(const String& prefix) const {
    return _path.startsWith(prefix);
}

String HttpRequest::pathSuffix(const String& prefix) const {
    if (!pathStartsWith(prefix)) {
        return "";
    }
    return _path.substring(prefix.length());
}

String HttpRequest::queryParam(const String& name) const {
    // Query params were removed from _path during parsing
    // This is a simplified implementation - real impl would need original path
    return "";
}

bool HttpRequest::hasQueryParam(const String& name) const {
    return queryParam(name).length() > 0;
}

String HttpRequest::toString() const {
    String result = "HttpRequest(";
    if (_valid) {
        result += "method=" + _method;
        result += ", path=" + _path;
        result += ", version=" + _version;
        result += ", body_len=" + String(_body.length());
    } else {
        result += "INVALID";
    }
    result += ")";
    return result;
}
