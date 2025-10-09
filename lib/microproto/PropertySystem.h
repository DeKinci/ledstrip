#ifndef MICROPROTO_PROPERTY_SYSTEM_H
#define MICROPROTO_PROPERTY_SYSTEM_H

#include <stdint.h>

namespace MicroProto {

class PropertySystem {
public:
    // Initialize system
    static void init();

    // Must be called in main loop for debouncing
    static void loop();

    // Mark property as dirty (called by PropertyBase)
    static void markDirty(uint8_t property_id, bool persistent);

    // Force immediate flush of all dirty properties
    static void flushAll();

    // Flush specific property
    static void flush(uint8_t property_id);

    // Load all persistent properties from NVS
    static void loadFromStorage();

    // Save all persistent properties to NVS
    static void saveToStorage();

    // Get number of registered properties
    static uint8_t getPropertyCount();

private:
    static constexpr uint32_t DEBOUNCE_MS = 1000; // 1 second debounce
    static constexpr uint16_t MAX_PROPERTIES = 256;

    static uint32_t lastChangeTime[MAX_PROPERTIES];
    static bool dirty[MAX_PROPERTIES];
    static uint8_t numProperties;

    // Internal flush implementation
    static void flushProperty(uint8_t property_id);
};

} // namespace MicroProto

#endif // MICROPROTO_PROPERTY_SYSTEM_H
