#ifndef MICROLOG_H
#define MICROLOG_H

#include "LogEntry.h"
#include <StreamProperty.h>

#ifndef MICROLOG_BUFFER_SIZE
#define MICROLOG_BUFFER_SIZE 10
#endif

namespace MicroLog {

/**
 * MicroLog - Centralized logging with MicroProto streaming.
 *
 * Two stream properties:
 *   errorLog  - Persistent ring buffer for WARN+ERROR (survives reboots)
 *   logStream - Online-only stream for INFO+WARN+ERROR (live when connected)
 *
 * Macro routing:
 *   LOG_DEBUG  → Serial only (compile-gated by DEBUG)
 *   LOG_INFO   → Serial + logStream
 *   LOG_WARN   → Serial + logStream + errorLog
 *   LOG_ERROR  → Serial + logStream + errorLog
 */
class MicroLog {
public:
    static void init();

    static void log(Level level, const char* tag, const char* fmt, ...)
        __attribute__((format(printf, 3, 4)));

    static uint16_t bootCount() { return _bootCount; }

    // Access the underlying stream properties (for custom wiring if needed)
    static MicroProto::StreamProperty<LogEntry, MICROLOG_BUFFER_SIZE>& errorLog() { return _errorLog; }
    static MicroProto::StreamProperty<LogEntry, 0>& logStream() { return _logStream; }

private:
    static uint16_t _bootCount;
    static bool _initialized;

    static MicroProto::StreamProperty<LogEntry, MICROLOG_BUFFER_SIZE> _errorLog;
    static MicroProto::StreamProperty<LogEntry, 0> _logStream;

    static void loadBootCount();
    static void formatAndPush(Level level, const char* tag, const char* formatted);
};

} // namespace MicroLog

// ============================================================================
// Logging macros
// ============================================================================

#ifdef ARDUINO
#include <Arduino.h>

// Timestamp formatter
inline const char* _microlog_timestamp() {
    static char buf[16];
    unsigned long ms = millis();
    unsigned long secs = ms / 1000;
    unsigned long mins = secs / 60;
    unsigned long hrs = mins / 60;
    snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu.%03lu",
             hrs % 100, mins % 60, secs % 60, ms % 1000);
    return buf;
}

#ifdef DEBUG
    #define LOG_DEBUG(tag, format, ...) \
        Serial.printf("[%s][DEBUG][%s] " format "\n", _microlog_timestamp(), tag, ##__VA_ARGS__)
#else
    #define LOG_DEBUG(tag, format, ...) ((void)0)
#endif

#define LOG_INFO(tag, format, ...) \
    MicroLog::MicroLog::log(MicroLog::LEVEL_INFO, tag, format, ##__VA_ARGS__)

#define LOG_WARN(tag, format, ...) \
    MicroLog::MicroLog::log(MicroLog::LEVEL_WARN, tag, format, ##__VA_ARGS__)

#define LOG_ERROR(tag, format, ...) \
    MicroLog::MicroLog::log(MicroLog::LEVEL_ERROR, tag, format, ##__VA_ARGS__)

#else
// Native build stubs (for tests)
#include <cstdio>

#define LOG_DEBUG(tag, format, ...) ((void)0)
#define LOG_INFO(tag, format, ...)  do { printf("[INFO][%s] " format "\n", tag, ##__VA_ARGS__); } while(0)
#define LOG_WARN(tag, format, ...)  do { printf("[WARN][%s] " format "\n", tag, ##__VA_ARGS__); } while(0)
#define LOG_ERROR(tag, format, ...) do { printf("[ERROR][%s] " format "\n", tag, ##__VA_ARGS__); } while(0)

#endif // ARDUINO

#endif // MICROLOG_H
