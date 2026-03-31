#include "HttpRequest.h"

HttpRequest::HttpRequest() : _valid(false) {}

void HttpRequest::reset() {
    _valid = false;
    _method = StringView();
    _path = StringView();
    _version = StringView();
    _body = StringView();
    _rawHeaders = StringView();
    clearPathParams();
    for (int i = 0; i < MAX_QUERY_PARAMS; i++) {
        _queryParamNames[i] = StringView();
        _queryParamValues[i] = StringView();
    }
    _queryParamCount = 0;
}

void HttpRequest::clearPathParams() {
    for (int i = 0; i < MAX_PATH_PARAMS; i++) {
        _pathParamNames[i] = StringView();
        _pathParamValues[i] = StringView();
    }
    _pathParamCount = 0;
}

bool HttpRequest::parse(const char* data, size_t len) {
    reset();

    if (!data || len == 0) {
        return false;
    }

    StringView raw(data, len);

    // Find header/body separator "\r\n\r\n"
    int headerEnd = -1;
    for (size_t i = 0; i + 3 < len; i++) {
        if (data[i] == '\r' && data[i+1] == '\n' &&
            data[i+2] == '\r' && data[i+3] == '\n') {
            headerEnd = i;
            break;
        }
    }

    if (headerEnd == -1) {
        return false;
    }

    // Extract body
    _body = StringView(data + headerEnd + 4, len - headerEnd - 4);

    // Split headers section
    StringView headerSection(data, headerEnd);

    // Find first line (request line)
    int firstLineEnd = -1;
    for (size_t i = 0; i + 1 < headerSection.length(); i++) {
        if (headerSection[i] == '\r' && headerSection[i+1] == '\n') {
            firstLineEnd = i;
            break;
        }
    }

    if (firstLineEnd == -1) {
        // Single line request (no additional headers)
        if (!parseRequestLine(headerSection)) {
            return false;
        }
    } else {
        StringView requestLine = headerSection.substr(0, firstLineEnd);
        if (!parseRequestLine(requestLine)) {
            return false;
        }
        // Store remaining headers
        _rawHeaders = headerSection.substr(firstLineEnd + 2);
    }

    _valid = true;
    return true;
}

bool HttpRequest::parseRequestLine(StringView line) {
    // Parse: METHOD PATH VERSION
    int firstSpace = line.find(' ');
    if (firstSpace == -1) {
        return false;
    }

    int secondSpace = line.find(' ', firstSpace + 1);
    if (secondSpace == -1) {
        return false;
    }

    _method = line.substr(0, firstSpace);
    _path = line.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    _version = line.substr(secondSpace + 1);

    // Extract and parse query string
    int queryStart = _path.find('?');
    if (queryStart != -1) {
        StringView queryString = _path.substr(queryStart + 1);
        _path = _path.substr(0, queryStart);
        parseQueryString(queryString);
    }

    return true;
}

void HttpRequest::parseQueryString(StringView queryString) {
    if (queryString.empty()) {
        return;
    }

    size_t pos = 0;
    while (pos < queryString.length() && _queryParamCount < MAX_QUERY_PARAMS) {
        // Find end of this param (& or end of string)
        int ampPos = queryString.find('&', pos);
        if (ampPos == -1) ampPos = queryString.length();

        StringView param = queryString.substr(pos, ampPos - pos);

        // Find = separator
        int eqPos = param.find('=');
        if (eqPos != -1) {
            _queryParamNames[_queryParamCount] = param.substr(0, eqPos);
            _queryParamValues[_queryParamCount] = param.substr(eqPos + 1);
            _queryParamCount++;
        } else if (!param.empty()) {
            // Key without value
            _queryParamNames[_queryParamCount] = param;
            _queryParamValues[_queryParamCount] = StringView();
            _queryParamCount++;
        }

        pos = ampPos + 1;
    }

    if (pos < queryString.length() && _queryParamCount >= MAX_QUERY_PARAMS) {
        Serial.printf("[HttpRequest] Warning: query param limit (%d) exceeded\n", MAX_QUERY_PARAMS);
    }
}

// Case-insensitive character comparison
static inline bool charEqualIgnoreCase(char a, char b) {
    if (a >= 'A' && a <= 'Z') a += 32;
    if (b >= 'A' && b <= 'Z') b += 32;
    return a == b;
}

StringView HttpRequest::header(StringView name) const {
    if (_rawHeaders.empty() || name.empty()) {
        return StringView();
    }

    // Case-insensitive search for "Name:"
    for (size_t i = 0; i + name.length() < _rawHeaders.length(); i++) {
        // Check for header name match
        bool match = true;
        for (size_t j = 0; j < name.length() && match; j++) {
            if (!charEqualIgnoreCase(_rawHeaders[j + i], name[j])) {
                match = false;
            }
        }

        // Check for colon after name
        if (match && _rawHeaders[i + name.length()] == ':') {
            size_t valueStart = i + name.length() + 1;

            // Skip whitespace after colon
            while (valueStart < _rawHeaders.length() &&
                   (_rawHeaders[valueStart] == ' ' || _rawHeaders[valueStart] == '\t')) {
                valueStart++;
            }

            // Find end of value (at \r\n or end of string)
            size_t valueEnd = valueStart;
            while (valueEnd < _rawHeaders.length() &&
                   _rawHeaders[valueEnd] != '\r' && _rawHeaders[valueEnd] != '\n') {
                valueEnd++;
            }

            return _rawHeaders.substr(valueStart, valueEnd - valueStart);
        }
    }

    return StringView();
}

bool HttpRequest::hasHeader(StringView name) const {
    return !header(name).empty();
}

bool HttpRequest::pathStartsWith(StringView prefix) const {
    return _path.startsWith(prefix);
}

StringView HttpRequest::pathSuffix(StringView prefix) const {
    if (!pathStartsWith(prefix)) {
        return StringView();
    }
    return _path.substr(prefix.length());
}

StringView HttpRequest::queryParam(StringView name) const {
    for (int i = 0; i < _queryParamCount; i++) {
        if (_queryParamNames[i] == name) {
            return _queryParamValues[i];
        }
    }
    return StringView();
}

bool HttpRequest::hasQueryParam(StringView name) const {
    for (int i = 0; i < _queryParamCount; i++) {
        if (_queryParamNames[i] == name) {
            return true;
        }
    }
    return false;
}

String HttpRequest::toString() const {
    if (!_valid) {
        return "[invalid request]";
    }
    String result = _method.toString();
    result += " ";
    result += _path.toString();
    if (!_body.empty()) {
        result += " [";
        result += String(_body.length());
        result += " bytes]";
    }
    return result;
}

// Helper: find next '/' or end of string
static size_t findSegmentEnd(StringView s, size_t start) {
    for (size_t i = start; i < s.length(); i++) {
        if (s[i] == '/') return i;
    }
    return s.length();
}

// Helper: compare segments
static bool segmentsEqual(StringView a, size_t aStart, size_t aEnd,
                          StringView b, size_t bStart, size_t bEnd) {
    size_t aLen = aEnd - aStart;
    size_t bLen = bEnd - bStart;
    if (aLen != bLen) return false;
    for (size_t i = 0; i < aLen; i++) {
        if (a[aStart + i] != b[bStart + i]) return false;
    }
    return true;
}

bool HttpRequest::match(StringView method, StringView pattern) {
    // Check method first
    if (_method != method) {
        return false;
    }

    // Clear previous path params
    clearPathParams();

    // Skip leading slashes
    size_t pathStart = (_path.length() > 0 && _path[0] == '/') ? 1 : 0;
    size_t patternStart = (pattern.length() > 0 && pattern[0] == '/') ? 1 : 0;

    // Calculate effective lengths (excluding trailing slashes)
    size_t pathLen = _path.length();
    size_t patternLen = pattern.length();
    if (pathLen > pathStart && _path[pathLen - 1] == '/') pathLen--;
    if (patternLen > patternStart && pattern[patternLen - 1] == '/') patternLen--;

    // Temporary storage for params during matching
    StringView tempNames[MAX_PATH_PARAMS];
    StringView tempValues[MAX_PATH_PARAMS];
    int tempCount = 0;

    size_t pathPos = pathStart;
    size_t patternPos = patternStart;

    while (patternPos < patternLen || pathPos < pathLen) {
        // If one ended but not the other
        if (patternPos >= patternLen || pathPos >= pathLen) {
            if (patternPos >= patternLen && pathPos >= pathLen) break;
            return false;
        }

        // Find segment boundaries
        size_t patternSegEnd = findSegmentEnd(pattern, patternPos);
        if (patternSegEnd > patternLen) patternSegEnd = patternLen;

        size_t pathSegEnd = findSegmentEnd(_path, pathPos);
        if (pathSegEnd > pathLen) pathSegEnd = pathLen;

        size_t patternSegLen = patternSegEnd - patternPos;
        size_t pathSegLen = pathSegEnd - pathPos;

        // Check if pattern segment is a path variable {name}
        if (patternSegLen >= 2 &&
            pattern[patternPos] == '{' &&
            pattern[patternSegEnd - 1] == '}') {
            // Path variable - path segment must be non-empty
            if (pathSegLen == 0) {
                return false;
            }
            // Store param
            if (tempCount < MAX_PATH_PARAMS) {
                tempNames[tempCount] = pattern.substr(patternPos + 1, patternSegLen - 2);
                tempValues[tempCount] = _path.substr(pathPos, pathSegLen);
                tempCount++;
            } else {
                Serial.printf("[HttpRequest] Warning: path param limit (%d) exceeded\n", MAX_PATH_PARAMS);
            }
        } else {
            // Literal segment - must match exactly
            if (!segmentsEqual(_path, pathPos, pathSegEnd, pattern, patternPos, patternSegEnd)) {
                return false;
            }
        }

        // Move past segment and slash
        patternPos = (patternSegEnd < patternLen) ? patternSegEnd + 1 : patternLen;
        pathPos = (pathSegEnd < pathLen) ? pathSegEnd + 1 : pathLen;
    }

    // Match successful - copy temp params to actual storage
    for (int i = 0; i < tempCount; i++) {
        _pathParamNames[i] = tempNames[i];
        _pathParamValues[i] = tempValues[i];
    }
    _pathParamCount = tempCount;

    return true;
}

StringView HttpRequest::pathParam(StringView name) const {
    for (int i = 0; i < _pathParamCount; i++) {
        if (_pathParamNames[i] == name) {
            return _pathParamValues[i];
        }
    }
    return StringView();
}