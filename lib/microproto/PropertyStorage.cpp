#include "PropertyStorage.h"
#include <Logger.h>
#include <Arduino.h>
#include <nvs_flash.h>
#include <nvs.h>

namespace MicroProto {

static const char* TAG = "PropertyStorage";

bool PropertyStorage::initialized = false;

void PropertyStorage::init() {
    if (initialized) return;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        LOG_WARN(TAG, "NVS partition issue, erasing and retrying");
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            LOG_ERROR(TAG, "Failed to erase NVS: %s", esp_err_to_name(err));
            initialized = true;
            return;
        }
        err = nvs_flash_init();
    }

    if (err != ESP_OK) {
        LOG_ERROR(TAG, "NVS initialization failed: %s", esp_err_to_name(err));
        initialized = true;
        return;
    }

    initialized = true;
    LOG_INFO(TAG, "NVS initialized");
}

// FNV-1a 32-bit hash → "p_" + 8 hex chars = 10 chars (well under NVS 15 char limit)
void PropertyStorage::makeKey(const char* name, char* buffer, size_t buffer_size) {
    uint32_t hash = 2166136261u;  // FNV offset basis
    for (const char* p = name; *p; p++) {
        hash ^= static_cast<uint8_t>(*p);
        hash *= 16777619u;  // FNV prime
    }
    snprintf(buffer, buffer_size, "p_%08x", hash);
}

bool PropertyStorage::save(PropertyBase* property) {
    if (!property) return false;
    if (!initialized) init();

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return false;
    }

    char key[16];
    makeKey(property->name, key, sizeof(key));

    const void* data = property->getData();
    size_t size = property->getSize();

    err = nvs_set_blob(handle, key, data, size);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to save %s: %s", property->name, esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err == ESP_OK) {
        LOG_INFO(TAG, "Saved %s (%zu bytes)", property->name, size);
        return true;
    } else {
        LOG_ERROR(TAG, "Failed to commit %s: %s", property->name, esp_err_to_name(err));
        return false;
    }
}

bool PropertyStorage::load(PropertyBase* property) {
    if (!property) return false;
    if (!initialized) init();

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        LOG_DEBUG(TAG, "NVS namespace not found (first boot?)");
        return false;
    }

    char key[16];
    makeKey(property->name, key, sizeof(key));

    // Get size first
    size_t size = 0;
    err = nvs_get_blob(handle, key, nullptr, &size);
    if (err != ESP_OK || size == 0) {
        nvs_close(handle);
        LOG_DEBUG(TAG, "%s not found in storage", property->name);
        return false;
    }

    // Allocate buffer and read
    uint8_t* buffer = new uint8_t[size];
    err = nvs_get_blob(handle, key, buffer, &size);
    nvs_close(handle);

    if (err == ESP_OK) {
        property->setData(buffer, size);
        LOG_INFO(TAG, "Loaded %s (%zu bytes)", property->name, size);
        delete[] buffer;
        return true;
    } else {
        LOG_ERROR(TAG, "Failed to load %s: %s", property->name, esp_err_to_name(err));
        delete[] buffer;
        return false;
    }
}

bool PropertyStorage::saveRaw(const char* propertyName, const void* data, size_t size) {
    if (!data || size == 0 || !propertyName) return false;
    if (!initialized) init();

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return false;
    }

    char key[16];
    makeKey(propertyName, key, sizeof(key));

    err = nvs_set_blob(handle, key, data, size);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to save raw %s: %s", propertyName, esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err == ESP_OK) {
        LOG_INFO(TAG, "Saved raw %s (%zu bytes)", propertyName, size);
        return true;
    } else {
        LOG_ERROR(TAG, "Failed to commit raw %s: %s", propertyName, esp_err_to_name(err));
        return false;
    }
}

size_t PropertyStorage::loadRaw(const char* propertyName, void* buffer, size_t bufferSize) {
    if (!buffer || bufferSize == 0 || !propertyName) return 0;
    if (!initialized) init();

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        LOG_DEBUG(TAG, "NVS namespace not found (first boot?)");
        return 0;
    }

    char key[16];
    makeKey(propertyName, key, sizeof(key));

    size_t size = 0;
    err = nvs_get_blob(handle, key, nullptr, &size);
    if (err != ESP_OK || size == 0) {
        nvs_close(handle);
        LOG_DEBUG(TAG, "Raw %s not found in storage", propertyName);
        return 0;
    }

    if (size > bufferSize) {
        size = bufferSize;
    }

    err = nvs_get_blob(handle, key, buffer, &size);
    nvs_close(handle);

    if (err == ESP_OK) {
        LOG_INFO(TAG, "Loaded raw %s (%zu bytes)", propertyName, size);
        return size;
    } else {
        LOG_ERROR(TAG, "Failed to load raw %s: %s", propertyName, esp_err_to_name(err));
        return 0;
    }
}

bool PropertyStorage::erase(PropertyBase* property) {
    if (!property) return false;
    if (!initialized) init();

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return false;

    char key[16];
    makeKey(property->name, key, sizeof(key));

    err = nvs_erase_key(handle, key);
    nvs_commit(handle);
    nvs_close(handle);

    return (err == ESP_OK);
}

bool PropertyStorage::eraseAll() {
    if (!initialized) init();

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return false;

    err = nvs_erase_all(handle);
    nvs_commit(handle);
    nvs_close(handle);

    LOG_WARN(TAG, "Erased all properties");
    return (err == ESP_OK);
}

static constexpr const char* SCHEMA_VERSION_KEY = "_sv";

void PropertyStorage::initSchemaVersion() {
    if (!initialized) init();

    // Load saved {version, propertyCount} pair
    struct { uint16_t version; uint8_t propertyCount; } saved = {0, 0};
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        size_t size = sizeof(saved);
        nvs_get_blob(handle, SCHEMA_VERSION_KEY, &saved, &size);
        nvs_close(handle);
    }

    // If property count changed (added/removed properties in firmware), bump version
    if (saved.version == 0 || PropertyBase::count != saved.propertyCount) {
        PropertyBase::schemaVersion = saved.version + 1;

        err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
        if (err == ESP_OK) {
            struct { uint16_t version; uint8_t propertyCount; } toSave = {
                PropertyBase::schemaVersion, PropertyBase::count
            };
            nvs_set_blob(handle, SCHEMA_VERSION_KEY, &toSave, sizeof(toSave));
            nvs_commit(handle);
            nvs_close(handle);
        }
        LOG_INFO(TAG, "Schema version bumped to %u (properties: %u -> %u)",
                 PropertyBase::schemaVersion, saved.propertyCount, PropertyBase::count);
    } else {
        PropertyBase::schemaVersion = saved.version;
        LOG_INFO(TAG, "Schema version %u (unchanged, %u properties)",
                 PropertyBase::schemaVersion, PropertyBase::count);
    }
}

} // namespace MicroProto
