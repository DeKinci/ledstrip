#ifndef MICROLOG_LOG_ENTRY_H
#define MICROLOG_LOG_ENTRY_H

#include <stdint.h>
#include <array>
#include <Reflect.h>

namespace MicroLog {

enum Level : uint8_t {
    LEVEL_DEBUG = 0,
    LEVEL_INFO  = 1,
    LEVEL_WARN  = 2,
    LEVEL_ERROR = 3
};

/**
 * LogEntry - Single log record stored in ring buffer and sent over MicroProto.
 *
 * Wire format: raw struct bytes (ObjectProperty-style encoding).
 * Fixed-size fields for zero-allocation ring buffer storage.
 *
 * Fields:
 *   timestamp  - millis() at log time
 *   bootCount  - distinguishes entries across reboots
 *   level      - DEBUG/INFO/WARN/ERROR
 *   _reserved  - reserved for future use (templateId for structured logs)
 *   tag        - source component tag (e.g., "WiFi", "BLE", "Proto")
 *   message    - formatted log message text
 */
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

#endif // MICROLOG_LOG_ENTRY_H
