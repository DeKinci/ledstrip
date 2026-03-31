#pragma once

// MicroLog + MicroProto integration.
// Adds StreamProperty sinks so logs flow to connected clients.
// Include this header and call MicroLogProto::init() to activate.

#include <MicroLog.h>
#include <StreamProperty.h>
#include <PropertyStorage.h>
#include <Reflect.h>
#include <array>

namespace MicroLog {

struct LogEntry {
    uint32_t timestamp;
    uint16_t bootCount;
    uint8_t  level;
    uint8_t  _reserved;
    std::array<char, 12> tag;
    std::array<char, 80> message;
};

static_assert(sizeof(LogEntry) == 100, "LogEntry must be exactly 100 bytes");

} // namespace MicroLog

MICROPROTO_FIELD_NAMES(MicroLog::LogEntry,
    "timestamp", "bootCount", "level", "_reserved", "tag", "message");

namespace MicroLog {

#ifndef MICROLOG_BUFFER_SIZE
#define MICROLOG_BUFFER_SIZE 10
#endif

// MicroProto streaming sink — pushes log entries to connected clients
class ProtoSink : public Sink {
public:
    void init();
    void onLog(Level level, const char* tag, const char* message) override;

    uint16_t bootCount() const { return _bootCount; }

    MicroProto::StreamProperty<LogEntry, MICROLOG_BUFFER_SIZE>& errorLog() { return _errorLog; }
    MicroProto::StreamProperty<LogEntry, 0>& logStream() { return _logStream; }

private:
    uint16_t _bootCount = 0;

    MicroProto::StreamProperty<LogEntry, MICROLOG_BUFFER_SIZE> _errorLog{
        "sys/errorLog",
        MicroProto::PropertyLevel::LOCAL,
        "Persistent error/warning log",
        MicroProto::UIHints()
            .setColor(MicroProto::UIColor::ROSE)
            .setWidget(MicroProto::Widget::Stream::LOG)
            .setIcon("\xE2\x9A\xA0"),
        MicroProto::PropertyBase::PERSISTENT, MicroProto::PropertyBase::READONLY
    };

    MicroProto::StreamProperty<LogEntry, 0> _logStream{
        "sys/logStream",
        MicroProto::PropertyLevel::LOCAL,
        "Live log stream",
        MicroProto::UIHints()
            .setColor(MicroProto::UIColor::SLATE)
            .setWidget(MicroProto::Widget::Stream::LOG),
        MicroProto::PropertyBase::NOT_PERSISTENT, MicroProto::PropertyBase::READONLY, MicroProto::PropertyBase::HIDDEN
    };

    void loadBootCount();
};

// Convenience: single global instance + init function
namespace MicroLogProto {
    ProtoSink& instance();
    void init();  // Calls Logger::addSink, loads boot count
}

} // namespace MicroLog
