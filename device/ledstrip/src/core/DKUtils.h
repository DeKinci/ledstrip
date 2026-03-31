#ifndef DEKINCI_UTILS_H
#define DEKINCI_UTILS_H

#include <Arduino.h>

String sformats(const char *fmt, va_list args);

String sformats(const char *fmt, ...);

#endif //DEKINCI_UTILS_H