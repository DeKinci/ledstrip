// Mock PropertySystem for native tests
// Provides stub implementations without Arduino/ESP32 dependencies

#include "PropertySystem.h"

namespace MicroProto {

// Static member definitions
DirtySet PropertySystem::_dirty;
DirtySet PropertySystem::_persistDirty;
uint32_t PropertySystem::_lastPersistTime[MAX_PROPERTIES] = {0};
uint8_t PropertySystem::_numProperties = 0;
std::array<FlushListener*, MICROPROTO_MAX_FLUSH_LISTENERS> PropertySystem::_flushListeners = {};

void PropertySystem::init() {
    _dirty.clearAll();
    _persistDirty.clearAll();
    _numProperties = 0;
}

void PropertySystem::loop() {
    // No-op in tests
}

void PropertySystem::markDirty(uint8_t property_id, bool persistent) {
    _dirty.set(property_id);
    if (persistent) {
        _persistDirty.set(property_id);
    }
}

void PropertySystem::loadFromStorage() {
    // No-op in tests
}

void PropertySystem::saveToStorage() {
    // No-op in tests
}

uint8_t PropertySystem::getPropertyCount() {
    return _numProperties;
}

bool PropertySystem::addFlushListener(FlushListener* listener) {
    if (!listener) return false;

    for (auto& slot : _flushListeners) {
        if (!slot) {
            slot = listener;
            return true;
        }
    }
    return false;
}

void PropertySystem::removeFlushListener(FlushListener* listener) {
    for (auto& slot : _flushListeners) {
        if (slot == listener) {
            slot = nullptr;
            return;
        }
    }
}

} // namespace MicroProto
