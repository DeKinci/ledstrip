#include "PropertyBase.h"
#include "PropertySystem.h"

namespace MicroProto {

// Static member initialization
PropertyBase* PropertyBase::head = nullptr;
uint8_t PropertyBase::nextId = 0;

PropertyBase::PropertyBase(
    const char* name,
    PropertyLevel level,
    bool persistent,
    bool readonly,
    bool hidden,
    bool ble_exposed,
    uint8_t group_id
) : id(nextId++),
    name(name),
    level(level),
    persistent(persistent),
    readonly(readonly),
    hidden(hidden),
    ble_exposed(ble_exposed),
    group_id(group_id)
{
    // Add to linked list
    next = head;
    head = this;
}

void PropertyBase::notifyChange() {
    // Call per-property callback immediately
    if (_onChange) {
        _onChange();
    }

    // Mark dirty for system-level processing (broadcast, persist, etc.)
    PropertySystem::markDirty(id, persistent);
}

} // namespace MicroProto
