#include "MicroLog.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <PropertyStorage.h>
#include <cstdarg>

namespace MicroLog {

using PB = MicroProto::PropertyBase;

// Static member definitions
uint16_t MicroLog::_bootCount = 0;
bool MicroLog::_initialized = false;

MicroProto::StreamProperty<LogEntry, MICROLOG_BUFFER_SIZE> MicroLog::_errorLog(
    "sys/errorLog",
    MicroProto::PropertyLevel::LOCAL,
    "Persistent error/warning log",
    MicroProto::UIHints()
        .setColor(MicroProto::UIColor::ROSE)
        .setWidget(MicroProto::Widget::Stream::LOG)
        .setIcon("\xE2\x9A\xA0"),  // ⚠
    PB::PERSISTENT, PB::READONLY
);

MicroProto::StreamProperty<LogEntry, 0> MicroLog::_logStream(
    "sys/logStream",
    MicroProto::PropertyLevel::LOCAL,
    "Live log stream",
    MicroProto::UIHints()
        .setColor(MicroProto::UIColor::SLATE)
        .setWidget(MicroProto::Widget::Stream::LOG),
    PB::NOT_PERSISTENT, PB::READONLY, PB::HIDDEN
);

static constexpr const char* NVS_BOOT_KEY = "log_bootcnt";

void MicroLog::init() {
    if (_initialized) return;
    _initialized = true;

    loadBootCount();
}

void MicroLog::loadBootCount() {
    uint16_t stored = 0;
    size_t loaded = MicroProto::PropertyStorage::loadRaw(NVS_BOOT_KEY, &stored, sizeof(stored));
    if (loaded == sizeof(stored)) {
        _bootCount = stored + 1;
    } else {
        _bootCount = 1;
    }
    MicroProto::PropertyStorage::saveRaw(NVS_BOOT_KEY, &_bootCount, sizeof(_bootCount));
}

void MicroLog::log(Level level, const char* tag, const char* fmt, ...) {
    // Format the message
    char msgBuf[80];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msgBuf, sizeof(msgBuf), fmt, args);
    va_end(args);

    // Serial output with timestamp
    Serial.printf("[%s][%s][%s] %s\n",
        _microlog_timestamp(),
        level == LEVEL_INFO  ? "INFO" :
        level == LEVEL_WARN  ? "WARN" :
        level == LEVEL_ERROR ? "ERROR" : "?",
        tag, msgBuf);

    formatAndPush(level, tag, msgBuf);
}

void MicroLog::formatAndPush(Level level, const char* tag, const char* formatted) {
    LogEntry entry{};
    entry.timestamp = millis();
    entry.bootCount = _bootCount;
    entry.level = level;
    entry._reserved = 0;

    // Copy tag (null-fill remaining)
    entry.tag.fill('\0');
    size_t tagLen = strlen(tag);
    if (tagLen > entry.tag.size()) tagLen = entry.tag.size();
    memcpy(entry.tag.data(), tag, tagLen);

    // Copy message (null-fill remaining)
    entry.message.fill('\0');
    size_t msgLen = strlen(formatted);
    if (msgLen > entry.message.size()) msgLen = entry.message.size();
    memcpy(entry.message.data(), formatted, msgLen);

    // Route to stream properties
    _logStream.push(entry);

    if (level >= LEVEL_WARN) {
        _errorLog.push(entry);
    }
}

} // namespace MicroLog

#endif // ARDUINO
