#pragma once

// Build info utilities for ESP32 firmware.
// Provides a short firmware hash (last 8 hex chars of ELF SHA256)
// useful for ETag cache invalidation, version display, etc.

#ifdef ARDUINO
#include <esp_app_desc.h>

namespace BuildInfo {

// Returns 8-char hex string (last 8 chars of firmware SHA256).
// Static buffer — safe to use as const char* without lifetime concerns.
inline const char* firmwareHash() {
    static char hash[9] = {0};
    if (hash[0]) return hash;

    const esp_app_desc_t* desc = esp_app_get_description();
    // Last 4 bytes of the 32-byte SHA256 → 8 hex chars
    const uint8_t* sha = desc->app_elf_sha256 + 28;
    for (int i = 0; i < 4; i++) {
        static const char hex[] = "0123456789abcdef";
        hash[i * 2]     = hex[sha[i] >> 4];
        hash[i * 2 + 1] = hex[sha[i] & 0x0f];
    }
    return hash;
}

// Returns compile date+time as "Mar 30 2026 14:23:01"
inline const char* buildDate() {
    const esp_app_desc_t* desc = esp_app_get_description();
    static char buf[48];
    if (buf[0]) return buf;
    snprintf(buf, sizeof(buf), "%s %s", desc->date, desc->time);
    return buf;
}

// Returns project name from ESP-IDF app descriptor
inline const char* projectName() {
    return esp_app_get_description()->project_name;
}

} // namespace BuildInfo

#else
// Native build stubs
namespace BuildInfo {
inline const char* firmwareHash() { return "00000000"; }
inline const char* buildDate() { return "native"; }
inline const char* projectName() { return "native"; }
}
#endif
