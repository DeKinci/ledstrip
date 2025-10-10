#include "WiFiCredentials.h"
#include <algorithm>
#include <nvs_flash.h>
#include <Logger.h>

namespace WiFiMan {

static const char* TAG = "WiFiCredentials";

const char* WiFiCredentials::PREF_NAMESPACE = "wifiman";
const char* WiFiCredentials::PREF_COUNT_KEY = "count";

WiFiCredentials::WiFiCredentials() {
    // Ensure NVS is initialized (idempotent - safe to call multiple times)
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated, erase and retry
        LOG_WARN(TAG, "NVS partition issue detected");
        // Don't erase here - let PropertyStorage handle it
        // We'll just reinit after it's fixed
        nvs_flash_init();
    }
}

WiFiCredentials::~WiFiCredentials() {
    prefs.end();
}

String WiFiCredentials::getSSIDKey(int index) const {
    return "ssid" + String(index);
}

String WiFiCredentials::getPasswordKey(int index) const {
    return "pass" + String(index);
}

String WiFiCredentials::getPriorityKey(int index) const {
    return "prio" + String(index);
}

String WiFiCredentials::getRSSIKey(int index) const {
    return "rssi" + String(index);
}

String WiFiCredentials::getLastConnectedKey(int index) const {
    return "last" + String(index);
}

bool WiFiCredentials::addNetwork(const String& ssid, const String& password, int priority) {
    if (ssid.isEmpty() || credentials.size() >= MAX_NETWORKS) {
        return false;
    }

    // Check if network already exists
    for (auto& cred : credentials) {
        if (cred.ssid == ssid) {
            cred.password = password;
            cred.priority = priority;
            return save();
        }
    }

    // Add new network
    credentials.push_back(NetworkCredential(ssid, password, priority));
    return save();
}

bool WiFiCredentials::removeNetwork(const String& ssid) {
    auto it = std::remove_if(credentials.begin(), credentials.end(),
        [&ssid](const NetworkCredential& cred) { return cred.ssid == ssid; });

    if (it != credentials.end()) {
        credentials.erase(it, credentials.end());
        return save();
    }
    return false;
}

bool WiFiCredentials::updatePriority(const String& ssid, int priority) {
    for (auto& cred : credentials) {
        if (cred.ssid == ssid) {
            cred.priority = priority;
            return save();
        }
    }
    return false;
}

void WiFiCredentials::clearAll() {
    credentials.clear();
    prefs.begin(PREF_NAMESPACE, false);
    prefs.clear();
    prefs.end();
}

bool WiFiCredentials::hasNetwork(const String& ssid) const {
    for (const auto& cred : credentials) {
        if (cred.ssid == ssid) {
            return true;
        }
    }
    return false;
}

NetworkCredential* WiFiCredentials::getNetwork(const String& ssid) {
    for (auto& cred : credentials) {
        if (cred.ssid == ssid) {
            return &cred;
        }
    }
    return nullptr;
}

void WiFiCredentials::updateRSSI(const String& ssid, int8_t rssi) {
    NetworkCredential* cred = getNetwork(ssid);
    if (cred) {
        cred->lastRSSI = rssi;
        save();
    }
}

void WiFiCredentials::updateLastConnected(const String& ssid, uint32_t timestamp) {
    NetworkCredential* cred = getNetwork(ssid);
    if (cred) {
        cred->lastConnected = timestamp;
        save();
    }
}

std::vector<NetworkCredential*> WiFiCredentials::getSortedNetworks() {
    std::vector<NetworkCredential*> sorted;
    for (auto& cred : credentials) {
        sorted.push_back(&cred);
    }

    // Sort by priority (desc), then by last connected (desc), then by RSSI (desc)
    std::sort(sorted.begin(), sorted.end(),
        [](const NetworkCredential* a, const NetworkCredential* b) {
            if (a->priority != b->priority) {
                return a->priority > b->priority;
            }
            if (a->lastConnected != b->lastConnected) {
                return a->lastConnected > b->lastConnected;
            }
            return a->lastRSSI > b->lastRSSI;
        });

    return sorted;
}

bool WiFiCredentials::load() {
    credentials.clear();

    if (!prefs.begin(PREF_NAMESPACE, true)) {
        LOG_ERROR(TAG, "Failed to open Preferences for reading");
        return false;
    }

    int count = prefs.getInt(PREF_COUNT_KEY, 0);
    LOG_INFO(TAG, "Loading %d saved networks", count);

    for (int i = 0; i < count && i < MAX_NETWORKS; i++) {
        NetworkCredential cred;
        cred.ssid = prefs.getString(getSSIDKey(i).c_str(), "");
        cred.password = prefs.getString(getPasswordKey(i).c_str(), "");
        cred.priority = prefs.getInt(getPriorityKey(i).c_str(), 0);
        cred.lastRSSI = prefs.getChar(getRSSIKey(i).c_str(), -100);
        cred.lastConnected = prefs.getUInt(getLastConnectedKey(i).c_str(), 0);

        if (!cred.ssid.isEmpty()) {
            LOG_DEBUG(TAG, "Loaded: '%s' (priority: %d)", cred.ssid.c_str(), cred.priority);
            credentials.push_back(cred);
        }
    }

    prefs.end();
    LOG_INFO(TAG, "Successfully loaded %d networks", credentials.size());
    return true;
}

bool WiFiCredentials::save() {
    if (!prefs.begin(PREF_NAMESPACE, false)) {
        LOG_ERROR(TAG, "Failed to open Preferences for writing");
        return false;
    }

    LOG_INFO(TAG, "Saving %d networks", credentials.size());
    prefs.clear();
    prefs.putInt(PREF_COUNT_KEY, credentials.size());

    for (size_t i = 0; i < credentials.size(); i++) {
        const NetworkCredential& cred = credentials[i];
        LOG_DEBUG(TAG, "Saving: '%s' (priority: %d)", cred.ssid.c_str(), cred.priority);

        size_t ssidLen = prefs.putString(getSSIDKey(i).c_str(), cred.ssid);
        size_t passLen = prefs.putString(getPasswordKey(i).c_str(), cred.password);
        size_t prioLen = prefs.putInt(getPriorityKey(i).c_str(), cred.priority);
        size_t rssiLen = prefs.putChar(getRSSIKey(i).c_str(), cred.lastRSSI);
        size_t lastLen = prefs.putUInt(getLastConnectedKey(i).c_str(), cred.lastConnected);

        if (ssidLen == 0 || passLen == 0) {
            LOG_ERROR(TAG, "Failed to write network %d", i);
        }
    }

    // Verify count was written
    int savedCount = prefs.getInt(PREF_COUNT_KEY, -1);
    if (savedCount != (int)credentials.size()) {
        LOG_WARN(TAG, "Count mismatch: wrote %d, read back %d", credentials.size(), savedCount);
    }

    prefs.end();
    LOG_INFO(TAG, "Save complete");
    return true;
}

} // namespace WiFiMan
