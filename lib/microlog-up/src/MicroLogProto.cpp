#include "MicroLogProto.h"

#ifdef ARDUINO
#include <Arduino.h>

namespace MicroLog {

static constexpr const char* NVS_BOOT_KEY = "log_bootcnt";

void ProtoSink::init() {
    loadBootCount();
    Logger::addSink(this);
}

void ProtoSink::loadBootCount() {
    uint16_t stored = 0;
    size_t loaded = MicroProto::PropertyStorage::loadRaw(NVS_BOOT_KEY, &stored, sizeof(stored));
    _bootCount = (loaded == sizeof(stored)) ? stored + 1 : 1;
    MicroProto::PropertyStorage::saveRaw(NVS_BOOT_KEY, &_bootCount, sizeof(_bootCount));
}

void ProtoSink::onLog(Level level, const char* tag, const char* message) {
    LogEntry entry{};
    entry.timestamp = millis();
    entry.bootCount = _bootCount;
    entry.level = level;
    entry._reserved = 0;

    entry.tag.fill('\0');
    size_t tagLen = strlen(tag);
    if (tagLen > entry.tag.size()) tagLen = entry.tag.size();
    memcpy(entry.tag.data(), tag, tagLen);

    entry.message.fill('\0');
    size_t msgLen = strlen(message);
    if (msgLen > entry.message.size()) msgLen = entry.message.size();
    memcpy(entry.message.data(), message, msgLen);

    _logStream.push(entry);
    if (level >= LEVEL_WARN) {
        _errorLog.push(entry);
    }
}

static ProtoSink _globalSink;

ProtoSink& MicroLogProto::instance() { return _globalSink; }

void MicroLogProto::init() {
    Logger::init();
    _globalSink.init();
}

} // namespace MicroLog

#endif // ARDUINO
