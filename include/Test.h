#ifndef TTTTTEST
#define TTTTTEST

#include <Arduino.h>

template <typename T>
class Testo {
private:
    uint16_t code;
    T t;
public:
    Testo(T t, uint16_t code) {
        Testo::t = t;
    }

    T get() {
        return t;
    }
};

#endif //TTTTTEST