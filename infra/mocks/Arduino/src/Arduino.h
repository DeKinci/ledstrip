#ifndef ARDUINO_H_MOCK
#define ARDUINO_H_MOCK

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>

// Forward declare StringView for conversion
class StringView;

// Arduino String class mock
class String {
public:
    String() : _str() {}
    String(const char* s) : _str(s ? s : "") {}
    String(const char* s, size_t len) : _str(s ? s : "", s ? len : 0) {}
    String(const String& s) : _str(s._str) {}
    String(int value) : _str(std::to_string(value)) {}
    String(unsigned int value) : _str(std::to_string(value)) {}
    String(long value) : _str(std::to_string(value)) {}
    String(unsigned long value) : _str(std::to_string(value)) {}

    String& operator=(const String& rhs) {
        _str = rhs._str;
        return *this;
    }

    String& operator=(const char* s) {
        _str = s ? s : "";
        return *this;
    }

    String operator+(const String& rhs) const {
        return String((_str + rhs._str).c_str());
    }

    String operator+(const char* rhs) const {
        return String((_str + rhs).c_str());
    }

    String& operator+=(const String& rhs) {
        _str += rhs._str;
        return *this;
    }

    String& operator+=(const char* rhs) {
        _str += rhs;
        return *this;
    }

    String& operator+=(char c) {
        _str += c;
        return *this;
    }

    void concat(const char* data, size_t len) {
        if (data && len > 0) {
            _str.append(data, len);
        }
    }

    bool operator==(const String& rhs) const { return _str == rhs._str; }
    bool operator==(const char* rhs) const { return _str == rhs; }
    bool operator!=(const String& rhs) const { return _str != rhs._str; }
    bool operator!=(const char* rhs) const { return _str != rhs; }

    char operator[](unsigned int index) const { return _str[index]; }

    const char* c_str() const { return _str.c_str(); }
    unsigned int length() const { return _str.length(); }

    bool startsWith(const String& prefix) const {
        return _str.find(prefix._str) == 0;
    }

    bool startsWith(const char* prefix) const {
        return _str.find(prefix) == 0;
    }

    bool endsWith(const String& suffix) const {
        if (suffix._str.length() > _str.length()) return false;
        return _str.compare(_str.length() - suffix._str.length(), suffix._str.length(), suffix._str) == 0;
    }

    bool endsWith(const char* suffix) const {
        size_t suffixLen = strlen(suffix);
        if (suffixLen > _str.length()) return false;
        return _str.compare(_str.length() - suffixLen, suffixLen, suffix) == 0;
    }

    int indexOf(char c, unsigned int fromIndex = 0) const {
        size_t pos = _str.find(c, fromIndex);
        return pos == std::string::npos ? -1 : (int)pos;
    }

    int indexOf(const String& s, unsigned int fromIndex = 0) const {
        size_t pos = _str.find(s._str, fromIndex);
        return pos == std::string::npos ? -1 : (int)pos;
    }

    int indexOf(const char* s, unsigned int fromIndex = 0) const {
        size_t pos = _str.find(s, fromIndex);
        return pos == std::string::npos ? -1 : (int)pos;
    }

    String substring(unsigned int beginIndex) const {
        if (beginIndex >= _str.length()) return String();
        return String(_str.substr(beginIndex).c_str());
    }

    String substring(unsigned int beginIndex, unsigned int endIndex) const {
        if (beginIndex >= _str.length()) return String();
        if (endIndex > _str.length()) endIndex = _str.length();
        if (beginIndex >= endIndex) return String();
        return String(_str.substr(beginIndex, endIndex - beginIndex).c_str());
    }

    void trim() {
        size_t start = _str.find_first_not_of(" \t\r\n");
        size_t end = _str.find_last_not_of(" \t\r\n");
        if (start == std::string::npos) {
            _str.clear();
        } else {
            _str = _str.substr(start, end - start + 1);
        }
    }

    void toLowerCase() {
        for (char& c : _str) {
            if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        }
    }

    int toInt() const {
        try { return std::stoi(_str); }
        catch (...) { return 0; }
    }

private:
    std::string _str;
};

inline String operator+(const char* lhs, const String& rhs) {
    return String(lhs) + rhs;
}

// Type definitions
using size_t = std::size_t;

// Min function
template<typename T>
T min(T a, T b) { return a < b ? a : b; }

template<typename T>
T max(T a, T b) { return a > b ? a : b; }

// Mock Serial class
class MockSerial {
public:
    void begin(unsigned long) {}
    void print(const char*) {}
    void print(const String& s) { print(s.c_str()); }
    void print(int) {}
    void println(const char*) {}
    void println(const String& s) { println(s.c_str()); }
    void println(int) {}
    void println() {}
    void printf(const char*, ...) {}
};

extern MockSerial Serial;

#endif // ARDUINO_H_MOCK