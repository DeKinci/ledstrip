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
    // Immediate callback
    if (changeCallback) {
        changeCallback(id);
    }

    // Mark dirty for debounced save/broadcast
    PropertySystem::markDirty(id, persistent);
}

} // namespace MicroProto
