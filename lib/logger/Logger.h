#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>

// Debug logging only works in debug builds
#ifdef DEBUG
    #define LOG_DEBUG(tag, format, ...) Serial.printf("[DEBUG][%s] " format "\n", tag, ##__VA_ARGS__)
#else
    #define LOG_DEBUG(tag, format, ...) ((void)0)
#endif

// Info logging always works
#define LOG_INFO(tag, format, ...) Serial.printf("[INFO][%s] " format "\n", tag, ##__VA_ARGS__)

// Warning logging always works
#define LOG_WARN(tag, format, ...) Serial.printf("[WARN][%s] " format "\n", tag, ##__VA_ARGS__)

// Error logging always works
#define LOG_ERROR(tag, format, ...) Serial.printf("[ERROR][%s] " format "\n", tag, ##__VA_ARGS__)

#endif // LOGGER_H
