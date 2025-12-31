#include "ResourceStorage.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#endif

namespace MicroProto {

bool ResourceStorage::_initialized = false;

bool ResourceStorage::init() {
#ifdef ARDUINO
    if (_initialized) return true;

    if (!SPIFFS.begin(true)) {  // Format on fail
        Serial.println("[ResourceStorage] SPIFFS mount failed");
        return false;
    }

    _initialized = true;
    Serial.println("[ResourceStorage] SPIFFS initialized");
    return true;
#else
    _initialized = true;
    return true;
#endif
}

void ResourceStorage::getBodyPath(const char* propName, uint32_t resourceId,
                                   char* buffer, size_t bufferSize) {
    snprintf(buffer, bufferSize, "/res/%s/%lu.bin",
             propName, static_cast<unsigned long>(resourceId));
}

bool ResourceStorage::writeBody(const char* propName, uint32_t resourceId,
                                 const void* data, size_t len) {
#ifdef ARDUINO
    if (!_initialized && !init()) return false;

    char path[64];
    getBodyPath(propName, resourceId, path, sizeof(path));

    // Ensure parent directory exists (create property dir)
    char dir[48];
    snprintf(dir, sizeof(dir), "/res/%s", propName);
    if (!SPIFFS.exists(dir)) {
        // SPIFFS creates directories implicitly when writing files
    }

    File file = SPIFFS.open(path, FILE_WRITE);
    if (!file) {
        Serial.printf("[ResourceStorage] Failed to open %s for write\n", path);
        return false;
    }

    size_t written = file.write(static_cast<const uint8_t*>(data), len);
    file.close();

    if (written != len) {
        Serial.printf("[ResourceStorage] Write failed: %zu/%zu bytes\n", written, len);
        return false;
    }

    Serial.printf("[ResourceStorage] Wrote %zu bytes to %s\n", len, path);
    return true;
#else
    // Native test environment - no SPIFFS
    (void)propName;
    (void)resourceId;
    (void)data;
    (void)len;
    return true;
#endif
}

size_t ResourceStorage::readBody(const char* propName, uint32_t resourceId,
                                  void* buffer, size_t bufferSize) {
#ifdef ARDUINO
    if (!_initialized && !init()) return 0;

    char path[64];
    getBodyPath(propName, resourceId, path, sizeof(path));

    File file = SPIFFS.open(path, FILE_READ);
    if (!file) {
        Serial.printf("[ResourceStorage] Failed to open %s for read\n", path);
        return 0;
    }

    size_t available = file.size();
    size_t toRead = (available < bufferSize) ? available : bufferSize;

    size_t bytesRead = file.read(static_cast<uint8_t*>(buffer), toRead);
    file.close();

    return bytesRead;
#else
    (void)propName;
    (void)resourceId;
    (void)buffer;
    (void)bufferSize;
    return 0;
#endif
}

bool ResourceStorage::deleteBody(const char* propName, uint32_t resourceId) {
#ifdef ARDUINO
    if (!_initialized && !init()) return false;

    char path[64];
    getBodyPath(propName, resourceId, path, sizeof(path));

    if (SPIFFS.exists(path)) {
        return SPIFFS.remove(path);
    }
    return true;  // Already doesn't exist
#else
    (void)propName;
    (void)resourceId;
    return true;
#endif
}

bool ResourceStorage::bodyExists(const char* propName, uint32_t resourceId) {
#ifdef ARDUINO
    if (!_initialized && !init()) return false;

    char path[64];
    getBodyPath(propName, resourceId, path, sizeof(path));

    return SPIFFS.exists(path);
#else
    (void)propName;
    (void)resourceId;
    return false;
#endif
}

size_t ResourceStorage::getBodySize(const char* propName, uint32_t resourceId) {
#ifdef ARDUINO
    if (!_initialized && !init()) return 0;

    char path[64];
    getBodyPath(propName, resourceId, path, sizeof(path));

    File file = SPIFFS.open(path, FILE_READ);
    if (!file) return 0;

    size_t size = file.size();
    file.close();
    return size;
#else
    (void)propName;
    (void)resourceId;
    return 0;
#endif
}

bool ResourceStorage::deleteAllBodies(const char* propName) {
#ifdef ARDUINO
    if (!_initialized && !init()) return false;

    char dir[48];
    snprintf(dir, sizeof(dir), "/res/%s", propName);

    File root = SPIFFS.open(dir);
    if (!root || !root.isDirectory()) {
        return true;  // Directory doesn't exist
    }

    File file = root.openNextFile();
    while (file) {
        char path[64];
        snprintf(path, sizeof(path), "%s/%s", dir, file.name());
        file.close();
        SPIFFS.remove(path);
        file = root.openNextFile();
    }

    return true;
#else
    (void)propName;
    return true;
#endif
}

} // namespace MicroProto