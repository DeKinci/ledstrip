#ifndef MICROCORE_STRING_VIEW_H
#define MICROCORE_STRING_VIEW_H

#include <Arduino.h>

class StringView {
    const char* _data = nullptr;
    size_t _len = 0;

public:
    constexpr StringView() = default;
    constexpr StringView(const char* s, size_t len) : _data(s), _len(len) {}
    StringView(const char* s) : _data(s), _len(s ? strlen(s) : 0) {}
    StringView(const String& s) : _data(s.c_str()), _len(s.length()) {}

    // Element access
    const char* data() const { return _data; }
    size_t length() const { return _len; }
    size_t size() const { return _len; }
    bool empty() const { return _len == 0; }
    char operator[](size_t i) const { return _data[i]; }
    char front() const { return _data[0]; }
    char back() const { return _data[_len - 1]; }

    // Iterators
    const char* begin() const { return _data; }
    const char* end() const { return _data + _len; }

    // Modifiers (just moves pointers, no allocation)
    void removePrefix(size_t n) {
        n = n > _len ? _len : n;
        _data += n;
        _len -= n;
    }

    void removeSuffix(size_t n) {
        _len = n > _len ? 0 : _len - n;
    }

    // Operations
    StringView substr(size_t pos, size_t count = SIZE_MAX) const {
        if (pos >= _len) return {};
        size_t rlen = count < (_len - pos) ? count : (_len - pos);
        return {_data + pos, rlen};
    }

    int find(char c, size_t pos = 0) const {
        for (size_t i = pos; i < _len; i++) {
            if (_data[i] == c) return i;
        }
        return -1;
    }

    int find(StringView sv, size_t pos = 0) const {
        if (sv._len == 0) return pos <= _len ? pos : -1;
        if (sv._len > _len) return -1;
        for (size_t i = pos; i <= _len - sv._len; i++) {
            if (memcmp(_data + i, sv._data, sv._len) == 0) return i;
        }
        return -1;
    }

    bool contains(char c) const { return find(c) >= 0; }
    bool contains(StringView sv) const { return find(sv) >= 0; }

    bool startsWith(StringView prefix) const {
        return _len >= prefix._len && memcmp(_data, prefix._data, prefix._len) == 0;
    }

    bool endsWith(StringView suffix) const {
        return _len >= suffix._len &&
               memcmp(_data + _len - suffix._len, suffix._data, suffix._len) == 0;
    }

    // Comparison
    bool operator==(StringView other) const {
        return _len == other._len && memcmp(_data, other._data, _len) == 0;
    }

    bool operator!=(StringView other) const { return !(*this == other); }

    int compare(StringView other) const {
        size_t minLen = _len < other._len ? _len : other._len;
        int result = memcmp(_data, other._data, minLen);
        if (result != 0) return result;
        return _len < other._len ? -1 : (_len > other._len ? 1 : 0);
    }

    bool operator<(StringView other) const { return compare(other) < 0; }

    // Conversion (only allocates here)
    String toString() const {
        if (!_data || _len == 0) return String();
        char* buf = new char[_len + 1];
        memcpy(buf, _data, _len);
        buf[_len] = '\0';
        String result(buf);
        delete[] buf;
        return result;
    }

    // Implicit conversion to String
    operator String() const { return toString(); }

    // Case-insensitive comparison
    bool equalsIgnoreCase(StringView other) const {
        if (_len != other._len) return false;
        for (size_t i = 0; i < _len; i++) {
            char a = _data[i];
            char b = other._data[i];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) return false;
        }
        return true;
    }
};

// String concatenation operators
// Only define the essential operators to avoid ambiguity with Arduino's StringSumHelper
inline String operator+(const String& lhs, StringView rhs) {
    String result = lhs;
    result.concat(rhs.data(), rhs.length());
    return result;
}

inline String operator+(const char* lhs, StringView rhs) {
    String result(lhs);
    result.concat(rhs.data(), rhs.length());
    return result;
}

#endif // MICROCORE_STRING_VIEW_H