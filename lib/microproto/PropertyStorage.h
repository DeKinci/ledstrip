#ifndef MICROPROTO_PROPERTY_STORAGE_H
#define MICROPROTO_PROPERTY_STORAGE_H

#include "PropertyBase.h"

namespace MicroProto {

class PropertyStorage {
public:
    // Initialize NVS
    static void init();

    // Save property to NVS
    static bool save(PropertyBase* property);

    // Load property from NVS
    static bool load(PropertyBase* property);

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
