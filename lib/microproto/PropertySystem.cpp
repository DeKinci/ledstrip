#include "PropertySystem.h"
#include "PropertyBase.h"
#include "PropertyStorage.h"
#include "Logger.h"
#include <Arduino.h>

namespace MicroProto {

static const char* TAG = "PropertySystem";

// Static member initialization
uint32_t PropertySystem::lastChangeTime[MAX_PROPERTIES] = {0};
bool PropertySystem::dirty[MAX_PROPERTIES] = {false};
uint8_t PropertySystem::numProperties = 0;

void PropertySystem::init() {
    LOG_DEBUG(TAG, "Starting init...");

    // Count registered properties
    numProperties = 0;
    LOG_DEBUG(TAG, "Counting properties...");
    for (PropertyBase* prop = PropertyBase::head; prop; prop = prop->next) {
        LOG_DEBUG(TAG, "  Property %d: %s (persistent=%d)", numProperties, prop->name, prop->persistent);
        numProperties++;
    }

    LOG_INFO(TAG, "Initialized with %d properties", numProperties);

    // Load persistent properties from NVS
    LOG_DEBUG(TAG, "Loading from storage...");
    loadFromStorage();
    LOG_DEBUG(TAG, "Init complete");
}

void PropertySystem::loop() {
    uint32_t now = millis();

    // Check for properties that need flushing
    for (uint8_t i = 0; i < numProperties; i++) {
        if (dirty[i] && (now - lastChangeTime[i]) >= DEBOUNCE_MS) {
            flushProperty(i);
            dirty[i] = false;
        }
    }
}

void PropertySystem::markDirty(uint8_t property_id, bool persistent) {
    if (property_id >= MAX_PROPERTIES) return;

    dirty[property_id] = true;
    lastChangeTime[property_id] = millis();
}

void PropertySystem::flush(uint8_t property_id) {
    if (property_id >= numProperties) return;

    flushProperty(property_id);
    dirty[property_id] = false;
}

void PropertySystem::flushAll() {
    for (uint8_t i = 0; i < numProperties; i++) {
        if (dirty[i]) {
            flushProperty(i);
            dirty[i] = false;
        }
    }
}

void PropertySystem::flushProperty(uint8_t property_id) {
    // Find property by id
    PropertyBase* prop = nullptr;
    for (PropertyBase* p = PropertyBase::head; p; p = p->next) {
        if (p->id == property_id) {
            prop = p;
            break;
        }
    }

    if (!prop) return;

    // Save to NVS if persistent
    if (prop->persistent) {
        PropertyStorage::save(prop);
    }

    // TODO: Broadcast to WebSocket clients
    // TODO: Broadcast to mesh if GROUP/GLOBAL

    LOG_DEBUG(TAG, "Flushed property %d (%s)", prop->id, prop->name);
}

void PropertySystem::loadFromStorage() {
    LOG_DEBUG(TAG, "loadFromStorage() start");
    int loaded = 0;
    for (PropertyBase* prop = PropertyBase::head; prop; prop = prop->next) {
        if (prop->persistent) {
            LOG_DEBUG(TAG, "  Loading property: %s (id=%d)", prop->name, prop->id);
            bool success = PropertyStorage::load(prop);
            if (success) loaded++;
        }
    }
    LOG_INFO(TAG, "Loaded %d properties from storage", loaded);
}

void PropertySystem::saveToStorage() {
    for (PropertyBase* prop = PropertyBase::head; prop; prop = prop->next) {
        if (prop->persistent) {
            PropertyStorage::save(prop);
        }
    }
}

uint8_t PropertySystem::getPropertyCount() {
    return numProperties;
}

} // namespace MicroProto
