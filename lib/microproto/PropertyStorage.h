#ifndef MICROPROTO_PROPERTY_STORAGE_H
#define MICROPROTO_PROPERTY_STORAGE_H

#include "PropertyBase.h"

namespace MicroProto {

class PropertyStorage {
public:
    // Initialize NVS
    static void init();

    // Save property to NVS (uses property->getData())
    static bool save(PropertyBase* property);

    // Load property from NVS (uses property->setData())
    static bool load(PropertyBase* property);

    // Save raw bytes to NVS for a property ID
    static bool saveRaw(uint8_t property_id, const void* data, size_t size);

    // Load raw bytes from NVS for a property ID
    // Returns bytes read, or 0 on failure
    static size_t loadRaw(uint8_t property_id, void* buffer, size_t bufferSize);

    // Erase property from NVS
    static bool erase(PropertyBase* property);

    // Erase all properties
    static bool eraseAll();

private:
    static constexpr const char* NVS_NAMESPACE = "microproto";
    static bool initialized;

    // Generate NVS key from property id
    static void makeKey(uint8_t property_id, char* buffer, size_t buffer_size);
};

} // namespace MicroProto

#endif // MICROPROTO_PROPERTY_STORAGE_H
