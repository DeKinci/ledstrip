// Mock PropertySystem for native tests
// Provides stub implementations without Arduino/ESP32 dependencies

#include "PropertySystem.h"

namespace MicroProto {

// Static member definitions
DirtySet PropertySystem::_dirty;
DirtySet PropertySystem::_persistDirty;
uint32_t PropertySystem::_lastPersistTime[MAX_PROPERTIES] = {0};
uint8_t PropertySystem::_numProperties = 0;
std::array<PropertySystem::FlushCallback, MICROPROTO_MAX_FLUSH_CALLBACKS> PropertySystem::_flushCallbacks;

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

int8_t PropertySystem::onFlush(FlushCallback callback) {
    for (size_t i = 0; i < _flushCallbacks.size(); ++i) {
        if (!_flushCallbacks[i]) {
            _flushCallbacks[i] = callback;
            return static_cast<int8_t>(i);
        }
    }
    return -1;
}

void PropertySystem::removeFlushCallback(int8_t slot) {
    if (slot >= 0 && static_cast<size_t>(slot) < _flushCallbacks.size()) {
        _flushCallbacks[slot].clear();
    }
}

} // namespace MicroProto