#include "PropertyBase.h"
#include "PropertySystem.h"
#ifdef ARDUINO
#include "PropertyStorage.h"
#endif

namespace MicroProto {

// Static member initialization
std::array<PropertyBase*, PropertyBase::MAX_PROPERTIES> PropertyBase::byId = {};
uint8_t PropertyBase::count = 0;
uint8_t PropertyBase::nextId = 0;

PropertyBase::PropertyBase(
    const char* name,
    PropertyLevel level,
    bool persistent,
    bool readonly,
    bool hidden,
    bool ble_exposed,
    uint8_t group_id,
    const char* description,
    UIHints uiHints
) : id(nextId++),
    name(name),
    description(description),
    level(level),
    persistent(persistent),
    readonly(readonly),
    hidden(hidden),
    ble_exposed(ble_exposed),
    group_id(group_id),
    ui(uiHints)
{
    // Register in lookup array
    byId[id] = this;
    count++;
}

void PropertyBase::notifyChange() {
    // Call per-property callback immediately
    if (_onChange) {
        _onChange();
    }

    // Mark dirty for system-level processing (broadcast, persist, etc.)
    PropertySystem::markDirty(id, persistent);
}

bool PropertyBase::saveToNVS() {
#ifdef ARDUINO
    return PropertyStorage::save(this);
#else
    return true;  // Stub for native tests
#endif
}

bool PropertyBase::loadFromNVS() {
#ifdef ARDUINO
    return PropertyStorage::load(this);
#else
    return false;  // Stub for native tests (no stored data)
#endif
}

} // namespace MicroProto
