// Mock ArduinoJson for native tests
#pragma once

#include <string>
#include <cstring>

// Minimal mock to satisfy webutils compilation
class JsonDocument {
public:
    bool containsKey(const char*) const { return false; }
};

class JsonObject;
class JsonArray;

class JsonVariant {
public:
    operator bool() const { return false; }
    template<typename T> T as() const { return T{}; }
};

class JsonObject {
public:
    JsonVariant operator[](const char*) { return JsonVariant{}; }
    bool containsKey(const char*) const { return false; }
};

class JsonArray {
public:
    size_t size() const { return 0; }
};

template<size_t N>
class StaticJsonDocument : public JsonDocument {
public:
    template<typename T> T as() { return T{}; }
    JsonObject to() { return JsonObject{}; }
};

enum DeserializationError_t {
    Ok = 0,
    EmptyInput,
    IncompleteInput,
    InvalidInput,
    NoMemory,
    TooDeep
};

class DeserializationError {
public:
    DeserializationError(DeserializationError_t e = Ok) : _error(e) {}
    operator bool() const { return _error != Ok; }
    const char* c_str() const { return "error"; }
private:
    DeserializationError_t _error;
};

template<typename TDocument>
DeserializationError deserializeJson(TDocument& doc, const char* input) {
    return DeserializationError(Ok);
}

template<typename TDocument>
DeserializationError deserializeJson(TDocument& doc, const std::string& input) {
    return DeserializationError(Ok);
}

template<typename TDocument>
size_t serializeJson(const TDocument& doc, char* output, size_t size) {
    return 0;
}

template<typename TDocument>
size_t serializeJson(const TDocument& doc, std::string& output) {
    return 0;
}