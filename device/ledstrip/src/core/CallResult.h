#ifndef GARLAND_CALL_RESULT_H
#define GARLAND_CALL_RESULT_H

#include <Arduino.h>
#include "DKUtils.h"

template <typename T>
class CallResult {
private:
    uint16_t code;
    String message;
    T value;
public:
    CallResult(T val, uint16_t cd = 200, const char* msg = nullptr, ...) {
        value = val;
        code = cd;

        if (msg != nullptr) {
            va_list args;
            va_start(args, msg);
            message = sformats(msg, args);
            va_end(args);
        } else {
            message = "";
        }
    }

    uint16_t getCode() {
        return code;
    }

    bool hasError() {
        return code >= 400;
    }

    String& getMessage() {
        return message;
    }

    T getValue() {
        return value;
    }
};


#endif //GARLAND_CALL_RESULT_H