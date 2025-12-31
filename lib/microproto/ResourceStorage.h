#ifndef MICROPROTO_RESOURCE_STORAGE_H
#define MICROPROTO_RESOURCE_STORAGE_H

#include <stdint.h>
#include <stddef.h>

namespace MicroProto {

/**
 * ResourceStorage - SPIFFS operations for resource bodies
 *
 * Handles reading/writing resource bodies to SPIFFS.
 * Bodies are stored at: /resources/{propName}/{resourceId}.bin
 */
class ResourceStorage {
public:
    /**
     * Initialize SPIFFS (call once at startup)
     * Returns true on success
     */
    static bool init();

    /**
     * Write resource body to SPIFFS
     */
    static bool writeBody(const char* propName, uint32_t resourceId,
                          const void* data, size_t len);

    /**
     * Read resource body from SPIFFS
     * Returns bytes read, or 0 on failure
     */
    static size_t readBody(const char* propName, uint32_t resourceId,
                           void* buffer, size_t bufferSize);

    /**
     * Delete resource body from SPIFFS
     */
    static bool deleteBody(const char* propName, uint32_t resourceId);

    /**
     * Check if resource body exists
     */
    static bool bodyExists(const char* propName, uint32_t resourceId);

    /**
     * Get body file size without reading
     */
    static size_t getBodySize(const char* propName, uint32_t resourceId);

    /**
     * Delete all bodies for a property
     */
    static bool deleteAllBodies(const char* propName);

    /**
     * Get path for a resource body file
     */
    static void getBodyPath(const char* propName, uint32_t resourceId,
                            char* buffer, size_t bufferSize);

private:
    static bool _initialized;
};

} // namespace MicroProto

#endif // MICROPROTO_RESOURCE_STORAGE_H