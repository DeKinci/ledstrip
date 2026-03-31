#ifndef URL_DECODE_H
#define URL_DECODE_H

#include <cstddef>
#include <cstdint>

// In-place URL decode. Decodes %XX sequences and + to space.
// Returns new length (always <= input length).
inline size_t urlDecode(char* str, size_t len) {
    size_t out = 0;
    for (size_t i = 0; i < len; i++) {
        if (str[i] == '%' && i + 2 < len) {
            auto hexVal = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                return -1;
            };
            int hi = hexVal(str[i + 1]);
            int lo = hexVal(str[i + 2]);
            if (hi >= 0 && lo >= 0) {
                str[out++] = (char)((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        if (str[i] == '+') {
            str[out++] = ' ';
            continue;
        }
        str[out++] = str[i];
    }
    return out;
}

#endif // URL_DECODE_H
