// Mock ResourceStorage for native tests
// Provides stub implementations without SPIFFS dependencies

#include "ResourceStorage.h"
#include <cstdio>

namespace MicroProto {

bool ResourceStorage::_initialized = false;

bool ResourceStorage::init() {
    _initialized = true;
    return true;
}

void ResourceStorage::getBodyPath(const char* propName, uint32_t resourceId,
                                   char* buffer, size_t bufferSize) {
    snprintf(buffer, bufferSize, "/res/%s/%lu.bin",
             propName, static_cast<unsigned long>(resourceId));
}

bool ResourceStorage::writeBody(const char* propName, uint32_t resourceId,
                                 const void* data, size_t len) {
    (void)propName;
    (void)resourceId;
    (void)data;
    (void)len;
    return true;  // Pretend success
}

size_t ResourceStorage::readBody(const char* propName, uint32_t resourceId,
                                  void* buffer, size_t bufferSize) {
    (void)propName;
    (void)resourceId;
    (void)buffer;
    (void)bufferSize;
    return 0;  // No SPIFFS in native
}

bool ResourceStorage::deleteBody(const char* propName, uint32_t resourceId) {
    (void)propName;
    (void)resourceId;
    return true;
}

bool ResourceStorage::bodyExists(const char* propName, uint32_t resourceId) {
    (void)propName;
    (void)resourceId;
    return false;
}

size_t ResourceStorage::getBodySize(const char* propName, uint32_t resourceId) {
    (void)propName;
    (void)resourceId;
    return 0;
}

bool ResourceStorage::deleteAllBodies(const char* propName) {
    (void)propName;
    return true;
}

} // namespace MicroProto