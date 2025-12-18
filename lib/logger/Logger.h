#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>

// Timestamp formatter - returns pointer to static buffer with hh:mm:ss.SSS format
inline const char* logTimestamp() {
    static char buf[16];  // "hh:mm:ss.SSS" = 12 chars + null
    unsigned long ms = millis();
    unsigned long secs = ms / 1000;
    unsigned long mins = secs / 60;
    unsigned long hrs = mins / 60;
    snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu.%03lu",
             hrs % 100, mins % 60, secs % 60, ms % 1000);
    buf[sizeof(buf) - 1] = '\0';  // Explicit null termination
    return buf;
}

// Debug logging only works in debug builds
#ifdef DEBUG
    #define LOG_DEBUG(tag, format, ...) Serial.printf("[%s][DEBUG][%s] " format "\n", logTimestamp(), tag, ##__VA_ARGS__)
#else
    #define LOG_DEBUG(tag, format, ...) ((void)0)
#endif

// Info logging always works
#define LOG_INFO(tag, format, ...) Serial.printf("[%s][INFO][%s] " format "\n", logTimestamp(), tag, ##__VA_ARGS__)

// Warning logging always works
#define LOG_WARN(tag, format, ...) Serial.printf("[%s][WARN][%s] " format "\n", logTimestamp(), tag, ##__VA_ARGS__)

// Error logging always works
#define LOG_ERROR(tag, format, ...) Serial.printf("[%s][ERROR][%s] " format "\n", logTimestamp(), tag, ##__VA_ARGS__)

#endif // LOGGER_H
