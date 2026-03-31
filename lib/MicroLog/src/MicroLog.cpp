#include "MicroLog.h"
#include "Gauge.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <cstdarg>

namespace MicroLog {

Sink* Logger::_sinks[MAX_SINKS] = {};
int Logger::_sinkCount = 0;
MetricBase* Logger::_metrics[MAX_METRICS] = {};
int Logger::_metricCount = 0;
bool Logger::_initialized = false;
uint32_t Logger::_metricsIntervalMs = MICROLOG_METRICS_INTERVAL_MS;
uint32_t Logger::_lastMetricsPrint = 0;

// MetricBase auto-registers with Logger
MetricBase::MetricBase(const char* name, const char* unit)
    : _name(name), _unit(unit) {
    Logger::addMetric(this);
}

void Logger::init() {
    if (_initialized) return;
    _initialized = true;
    _lastMetricsPrint = millis();
}

void Logger::addSink(Sink* sink) {
    if (_sinkCount < MAX_SINKS) _sinks[_sinkCount++] = sink;
}

void Logger::addMetric(MetricBase* metric) {
    if (_metricCount < MAX_METRICS) _metrics[_metricCount++] = metric;
}

void Logger::log(Level level, const char* tag, const char* fmt, ...) {
    char msgBuf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msgBuf, sizeof(msgBuf), fmt, args);
    va_end(args);

    Serial.printf("[%s][%s][%s] %s\n",
        _microlog_timestamp(),
        level == LEVEL_INFO  ? "INFO" :
        level == LEVEL_WARN  ? "WARN" :
        level == LEVEL_ERROR ? "ERROR" : "?",
        tag, msgBuf);

    for (int i = 0; i < _sinkCount; i++) {
        _sinks[i]->onLog(level, tag, msgBuf);
    }
}

void Logger::loop() {
    if (_metricCount == 0) return;

    uint32_t now = millis();
    if (now - _lastMetricsPrint < _metricsIntervalMs) return;
    _lastMetricsPrint = now;

    // Build single metrics line: "[00:01:30.000][METRICS] heap=125000 rssi=-45dBm ws=2 ble=1"
    char line[256];
    int pos = 0;

    for (int i = 0; i < _metricCount && pos < (int)sizeof(line) - 2; i++) {
        if (i > 0) line[pos++] = ' ';
        pos += _metrics[i]->printValue(line + pos, sizeof(line) - pos);
    }
    line[pos] = '\0';

    Serial.printf("[%s][METRICS] %s\n", _microlog_timestamp(), line);
}

} // namespace MicroLog

#endif
