#ifndef REQUEST_BUFFER_H
#define REQUEST_BUFFER_H

#include <cstddef>
#include <cstring>

#ifndef HTTP_REQUEST_BUFFER_SIZE
#define HTTP_REQUEST_BUFFER_SIZE 4096
#endif

class RequestBuffer {
    char _data[HTTP_REQUEST_BUFFER_SIZE];
    size_t _len = 0;

public:
    void reset() { _len = 0; }

    char* data() { return _data; }
    const char* data() const { return _data; }
    size_t length() const { return _len; }
    size_t capacity() const { return HTTP_REQUEST_BUFFER_SIZE; }
    size_t remaining() const { return HTTP_REQUEST_BUFFER_SIZE - _len; }

    // Write bytes, returns actual bytes written
    size_t write(const char* src, size_t n) {
        size_t toWrite = n < remaining() ? n : remaining();
        memcpy(_data + _len, src, toWrite);
        _len += toWrite;
        return toWrite;
    }

    // Write single byte
    bool write(char c) {
        if (_len >= HTTP_REQUEST_BUFFER_SIZE) return false;
        _data[_len++] = c;
        return true;
    }

    // Direct access for reading into buffer
    char* writePtr() { return _data + _len; }
    void advance(size_t n) { _len += n; }

    // Set length (for when external code writes directly)
    void setLength(size_t n) { _len = n < HTTP_REQUEST_BUFFER_SIZE ? n : HTTP_REQUEST_BUFFER_SIZE; }

    char operator[](size_t i) const { return _data[i]; }
};

#endif // REQUEST_BUFFER_H