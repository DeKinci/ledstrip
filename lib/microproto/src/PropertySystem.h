#ifndef MICROPROTO_PROPERTY_SYSTEM_H
#define MICROPROTO_PROPERTY_SYSTEM_H

#include <stdint.h>
#include <array>

#ifndef MICROPROTO_MAX_FLUSH_LISTENERS
#define MICROPROTO_MAX_FLUSH_LISTENERS 4
#endif

namespace MicroProto {

// Dirty set passed to flush listeners - simple bitset for up to 256 properties
class DirtySet {
public:
    void set(uint8_t id) { _bits[id / 32] |= (1u << (id % 32)); }
    void clear(uint8_t id) { _bits[id / 32] &= ~(1u << (id % 32)); }
    bool test(uint8_t id) const { return (_bits[id / 32] & (1u << (id % 32))) != 0; }
    void clearAll() { for (auto& b : _bits) b = 0; }
    bool any() const { for (auto b : _bits) if (b) return true; return false; }

private:
    uint32_t _bits[8] = {0};  // 256 bits
};

// Interface for receiving property change notifications
class FlushListener {
public:
    virtual ~FlushListener() = default;
    virtual void onPropertiesChanged(const DirtySet& dirty) = 0;
};

class PropertySystem {
public:
    // Initialize system
    static void init();

    // Must be called in main loop - notifies listeners of dirty properties
    static void loop();

    // Mark property as dirty (called by PropertyBase)
    static void markDirty(uint8_t property_id, bool persistent);

    // Load all persistent properties from NVS
    static void loadFromStorage();

    // Save all persistent properties to NVS
    static void saveToStorage();

    // Get number of registered properties
    static uint8_t getPropertyCount();

    // Register/unregister flush listener - called each loop() with dirty set
    static bool addFlushListener(FlushListener* listener);
    static void removeFlushListener(FlushListener* listener);

private:
    static constexpr uint16_t MAX_PROPERTIES = 256;

    static DirtySet _dirty;
    static DirtySet _persistDirty;  // Separate tracking for persistence
    static uint32_t _lastPersistTime[MAX_PROPERTIES];
    static uint8_t _numProperties;

    static std::array<FlushListener*, MICROPROTO_MAX_FLUSH_LISTENERS> _flushListeners;

    static constexpr uint32_t PERSIST_DEBOUNCE_MS = 1000;
};

} // namespace MicroProto

#endif // MICROPROTO_PROPERTY_SYSTEM_H
