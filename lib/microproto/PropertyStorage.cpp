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
        // NVS partition was truncated, erase and retry
        LOG_WARN(TAG, "NVS partition issue, erasing and retrying");
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            LOG_ERROR(TAG, "Failed to erase NVS: %s", esp_err_to_name(err));
            // Continue anyway, mark as initialized to prevent crashes
            initialized = true;
            return;
        }
        err = nvs_flash_init();
    }

    if (err != ESP_OK) {
        LOG_ERROR(TAG, "NVS initialization failed: %s", esp_err_to_name(err));
        // Mark as initialized to prevent repeated init attempts and crashes
        initialized = true;
        return;
    }

    initialized = true;
    LOG_INFO(TAG, "NVS initialized");
}

void PropertyStorage::makeKey(uint8_t property_id, char* buffer, size_t buffer_size) {
    snprintf(buffer, buffer_size, "p%d", property_id);
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
    makeKey(property->id, key, sizeof(key));

    const void* data = property->getData();
    size_t size = property->getSize();

    err = nvs_set_blob(handle, key, data, size);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to save property %d: %s", property->id, esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err == ESP_OK) {
        LOG_INFO(TAG, "Saved property %d (%s)", property->id, property->name);
        return true;
    } else {
        LOG_ERROR(TAG, "Failed to commit property %d: %s", property->id, esp_err_to_name(err));
        return false;
    }
}

bool PropertyStorage::load(PropertyBase* property) {
    if (!property) return false;
    if (!initialized) init();

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        // Namespace doesn't exist yet, not an error
        LOG_DEBUG(TAG, "NVS namespace not found (first boot?)");
        return false;
    }

    char key[16];
    makeKey(property->id, key, sizeof(key));

    // Get size first
    size_t size = 0;
    err = nvs_get_blob(handle, key, nullptr, &size);
    if (err != ESP_OK || size == 0) {
        nvs_close(handle);
        LOG_DEBUG(TAG, "Property %d (%s) not found in storage", property->id, property->name);
        return false;
    }

    // Check size matches
    if (size != property->getSize()) {
        LOG_ERROR(TAG, "Size mismatch for property %d: expected %d, got %d",
                 property->id, property->getSize(), size);
        nvs_close(handle);
        return false;
    }

    // Allocate buffer and read
    uint8_t* buffer = new uint8_t[size];
    err = nvs_get_blob(handle, key, buffer, &size);
    nvs_close(handle);

    if (err == ESP_OK) {
        property->setData(buffer, size);
        LOG_INFO(TAG, "Loaded property %d (%s)", property->id, property->name);
        delete[] buffer;
        return true;
    } else {
        LOG_ERROR(TAG, "Failed to load property %d: %s", property->id, esp_err_to_name(err));
        delete[] buffer;
        return false;
    }
}

bool PropertyStorage::saveRaw(uint8_t property_id, const void* data, size_t size) {
    if (!data || size == 0) return false;
    if (!initialized) init();

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return false;
    }

    char key[16];
    makeKey(property_id, key, sizeof(key));

    err = nvs_set_blob(handle, key, data, size);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to save raw property %d: %s", property_id, esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err == ESP_OK) {
        LOG_INFO(TAG, "Saved raw property %d (%zu bytes)", property_id, size);
        return true;
    } else {
        LOG_ERROR(TAG, "Failed to commit raw property %d: %s", property_id, esp_err_to_name(err));
        return false;
    }
}

size_t PropertyStorage::loadRaw(uint8_t property_id, void* buffer, size_t bufferSize) {
    if (!buffer || bufferSize == 0) return 0;
    if (!initialized) init();

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        LOG_DEBUG(TAG, "NVS namespace not found (first boot?)");
        return 0;
    }

    char key[16];
    makeKey(property_id, key, sizeof(key));

    // Get size first
    size_t size = 0;
    err = nvs_get_blob(handle, key, nullptr, &size);
    if (err != ESP_OK || size == 0) {
        nvs_close(handle);
        LOG_DEBUG(TAG, "Raw property %d not found in storage", property_id);
        return 0;
    }

    // Limit to buffer size
    if (size > bufferSize) {
        size = bufferSize;
    }

    err = nvs_get_blob(handle, key, buffer, &size);
    nvs_close(handle);

    if (err == ESP_OK) {
        LOG_INFO(TAG, "Loaded raw property %d (%zu bytes)", property_id, size);
        return size;
    } else {
        LOG_ERROR(TAG, "Failed to load raw property %d: %s", property_id, esp_err_to_name(err));
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
    makeKey(property->id, key, sizeof(key));

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

} // namespace MicroProto
