#include "PropertySystem.h"
#include "PropertyBase.h"
#include "PropertyStorage.h"
#include "Logger.h"
#include <Arduino.h>

namespace MicroProto {

static const char* TAG = "PropertySystem";

// Static member initialization
DirtySet PropertySystem::_dirty;
DirtySet PropertySystem::_persistDirty;
uint32_t PropertySystem::_lastPersistTime[MAX_PROPERTIES] = {0};
uint8_t PropertySystem::_numProperties = 0;
std::array<PropertySystem::FlushCallback, MICROPROTO_MAX_FLUSH_CALLBACKS> PropertySystem::_flushCallbacks;

void PropertySystem::init() {
    LOG_DEBUG(TAG, "Starting init...");

    // Get property count from registry
    _numProperties = PropertyBase::count;
    LOG_DEBUG(TAG, "Found %d registered properties", _numProperties);

    for (uint8_t i = 0; i < _numProperties; i++) {
        PropertyBase* prop = PropertyBase::byId[i];
        if (prop) {
            LOG_DEBUG(TAG, "  Property %d: %s (persistent=%d)", i, prop->name, prop->persistent);
        }
    }

    LOG_INFO(TAG, "Initialized with %d properties", _numProperties);

    // Load persistent properties from NVS
    LOG_DEBUG(TAG, "Loading from storage...");
    loadFromStorage();
    LOG_DEBUG(TAG, "Init complete");
}

void PropertySystem::loop() {
    // Notify all flush callbacks if anything is dirty
    if (_dirty.any()) {
        for (auto& cb : _flushCallbacks) {
            if (cb) {
                cb(_dirty);
            }
        }
        _dirty.clearAll();
    }

    // Handle persistence with debouncing
    uint32_t now = millis();
    for (uint8_t i = 0; i < _numProperties; i++) {
        if (_persistDirty.test(i) && (now - _lastPersistTime[i]) >= PERSIST_DEBOUNCE_MS) {
            // Direct O(1) lookup
            PropertyBase* p = PropertyBase::byId[i];
            if (p && p->persistent) {
                p->saveToNVS();  // Use virtual method for proper serialization
                LOG_DEBUG(TAG, "Persisted property %d (%s)", p->id, p->name);
            }
            _persistDirty.clear(i);
        }
    }
}

void PropertySystem::markDirty(uint8_t property_id, bool persistent) {
    if (property_id >= MAX_PROPERTIES) return;

    _dirty.set(property_id);

    if (persistent) {
        _persistDirty.set(property_id);
        _lastPersistTime[property_id] = millis();
    }
}

void PropertySystem::loadFromStorage() {
    LOG_DEBUG(TAG, "loadFromStorage() start");
    int loaded = 0;
    for (uint8_t i = 0; i < PropertyBase::count; i++) {
        PropertyBase* prop = PropertyBase::byId[i];
        if (prop && prop->persistent) {
            LOG_DEBUG(TAG, "  Loading property: %s (id=%d)", prop->name, prop->id);
            bool success = prop->loadFromNVS();  // Use virtual method for proper deserialization
            if (success) loaded++;
        }
    }
    LOG_INFO(TAG, "Loaded %d properties from storage", loaded);
}

void PropertySystem::saveToStorage() {
    for (uint8_t i = 0; i < PropertyBase::count; i++) {
        PropertyBase* prop = PropertyBase::byId[i];
        if (prop && prop->persistent) {
            prop->saveToNVS();  // Use virtual method for proper serialization
        }
    }
}

uint8_t PropertySystem::getPropertyCount() {
    return _numProperties;
}

int8_t PropertySystem::onFlush(FlushCallback callback) {
    if (!callback) return -1;

    for (size_t i = 0; i < _flushCallbacks.size(); i++) {
        if (!_flushCallbacks[i]) {
            _flushCallbacks[i] = callback;
            return static_cast<int8_t>(i);
        }
    }
    return -1;
}

void PropertySystem::removeFlushCallback(int8_t slot) {
    if (slot >= 0 && static_cast<size_t>(slot) < _flushCallbacks.size()) {
        _flushCallbacks[slot] = FlushCallback{};
    }
}

} // namespace MicroProto
