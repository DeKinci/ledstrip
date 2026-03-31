#ifndef RESPONSE_BUFFER_H
#define RESPONSE_BUFFER_H

#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdarg>

#ifndef HTTP_RESPONSE_BUFFER_SIZE
#define HTTP_RESPONSE_BUFFER_SIZE 4096
#endif

class ResponseBuffer {
    char _data[HTTP_RESPONSE_BUFFER_SIZE];
    size_t _len = 0;

public:
    void reset() { _len = 0; }

    char* data() { return _data; }
    const char* data() const { return _data; }
    size_t length() const { return _len; }
    size_t capacity() const { return HTTP_RESPONSE_BUFFER_SIZE; }
    size_t remaining() const { return HTTP_RESPONSE_BUFFER_SIZE - _len; }

    // Get write pointer (for external writers like serializeJson)
    char* writePtr() { return _data + _len; }
    void advance(size_t n) {
        _len += n;
        if (_len > HTTP_RESPONSE_BUFFER_SIZE) _len = HTTP_RESPONSE_BUFFER_SIZE;
    }

    // Allocate n bytes from the buffer. Returns pointer or nullptr if full.
    char* alloc(size_t n) {
        if (n > remaining()) return nullptr;
        char* ptr = _data + _len;
        _len += n;
        return ptr;
    }

    // Write a null-terminated string. Returns bytes written (excluding null).
    size_t write(const char* str) {
        if (!str) return 0;
        size_t n = strlen(str);
        if (n > remaining()) n = remaining();
        memcpy(_data + _len, str, n);
        _len += n;
        return n;
    }

    // Write n bytes.
    size_t write(const char* src, size_t n) {
        if (n > remaining()) n = remaining();
        memcpy(_data + _len, src, n);
        _len += n;
        return n;
    }

    // Printf into buffer. Returns bytes written.
    size_t printf(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        int written = vsnprintf(_data + _len, remaining(), fmt, args);
        va_end(args);
        if (written > 0 && (size_t)written < remaining()) {
            _len += written;
            return written;
        }
        return 0;
    }
};

#endif // RESPONSE_BUFFER_H
