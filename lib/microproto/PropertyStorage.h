#ifndef MICROPROTO_PROPERTY_STORAGE_H
#define MICROPROTO_PROPERTY_STORAGE_H

#include "PropertyBase.h"

namespace MicroProto {

class PropertyStorage {
public:
    // Initialize NVS
    static void init();

    // Save property to NVS (uses property name as key)
    static bool save(PropertyBase* property);

    // Load property from NVS (uses property name as key)
    static bool load(PropertyBase* property);

    // Save raw bytes to NVS for a named property
    static bool saveRaw(const char* propertyName, const void* data, size_t size);

    // Load raw bytes from NVS for a named property
    // Returns bytes read, or 0 on failure
    static size_t loadRaw(const char* propertyName, void* buffer, size_t bufferSize);

    // Erase property from NVS
    static bool erase(PropertyBase* property);

    // Erase all properties
    static bool eraseAll();

    // Schema version persistence
    // Call after all properties are registered. Loads saved version,
    // compares with PropertyBase::schemaVersion (bumped by constructors),
    // and persists the new version if it changed.
    static void initSchemaVersion();

private:
    static constexpr const char* NVS_NAMESPACE = "microproto";
    static bool initialized;

    // Generate NVS key from property name (FNV-1a hash → "p_XXXXXXXX")
    static void makeKey(const char* name, char* buffer, size_t buffer_size);
};

} // namespace MicroProto

#endif // MICROPROTO_PROPERTY_STORAGE_H
