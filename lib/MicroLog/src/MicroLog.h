#pragma once

#include <cstdint>
#include <cstddef>

#ifndef MICROLOG_METRICS_INTERVAL_MS
#define MICROLOG_METRICS_INTERVAL_MS 30000
#endif

namespace MicroLog {

enum Level : uint8_t {
    LEVEL_DEBUG = 0,
    LEVEL_INFO  = 1,
    LEVEL_WARN  = 2,
    LEVEL_ERROR = 3
};

class MetricBase;  // Forward decl

class Sink {
public:
    virtual ~Sink() = default;
    virtual void onLog(Level level, const char* tag, const char* message) = 0;
};

class Logger {
public:
    static void init();
    static void loop();  // Call from main loop — prints metrics periodically

    static void log(Level level, const char* tag, const char* fmt, ...)
        __attribute__((format(printf, 3, 4)));

    static void addSink(Sink* sink);
    static void addMetric(MetricBase* metric);

    static void setMetricsInterval(uint32_t ms) { _metricsIntervalMs = ms; }

private:
    static constexpr int MAX_SINKS = 4;
    static constexpr int MAX_METRICS = 16;
    static Sink* _sinks[MAX_SINKS];
    static int _sinkCount;
    static MetricBase* _metrics[MAX_METRICS];
    static int _metricCount;
    static bool _initialized;
    static uint32_t _metricsIntervalMs;
    static uint32_t _lastMetricsPrint;
};

} // namespace MicroLog

#ifdef ARDUINO
#include <Arduino.h>

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
    MicroLog::Logger::log(MicroLog::LEVEL_INFO, tag, format, ##__VA_ARGS__)

#define LOG_WARN(tag, format, ...) \
    MicroLog::Logger::log(MicroLog::LEVEL_WARN, tag, format, ##__VA_ARGS__)

#define LOG_ERROR(tag, format, ...) \
    MicroLog::Logger::log(MicroLog::LEVEL_ERROR, tag, format, ##__VA_ARGS__)

#else
#include <cstdio>

#define LOG_DEBUG(tag, format, ...) ((void)0)
#define LOG_INFO(tag, format, ...)  do { printf("[INFO][%s] " format "\n", tag, ##__VA_ARGS__); } while(0)
#define LOG_WARN(tag, format, ...)  do { printf("[WARN][%s] " format "\n", tag, ##__VA_ARGS__); } while(0)
#define LOG_ERROR(tag, format, ...) do { printf("[ERROR][%s] " format "\n", tag, ##__VA_ARGS__); } while(0)

#endif
