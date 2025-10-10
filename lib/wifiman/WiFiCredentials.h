#ifndef WIFIMAN_CREDENTIALS_H
#define WIFIMAN_CREDENTIALS_H

#include <Arduino.h>
#include <vector>
#include <Preferences.h>

namespace WiFiMan {

struct NetworkCredential {
    String ssid;
    String password;
    int priority;        // Higher priority = try first
    int8_t lastRSSI;     // Last known signal strength
    uint32_t lastConnected; // Timestamp of last successful connection

    NetworkCredential()
        : priority(0), lastRSSI(-100), lastConnected(0) {}

    NetworkCredential(const String& s, const String& p, int prio = 0)
        : ssid(s), password(p), priority(prio), lastRSSI(-100), lastConnected(0) {}
};

class WiFiCredentials {
public:
    WiFiCredentials();
    ~WiFiCredentials();

    // Add/remove credentials
    bool addNetwork(const String& ssid, const String& password, int priority = 0);
    bool removeNetwork(const String& ssid);
    bool updatePriority(const String& ssid, int priority);
    void clearAll();

    // Query credentials
    const std::vector<NetworkCredential>& getAll() const { return credentials; }
    bool hasNetwork(const String& ssid) const;
    NetworkCredential* getNetwork(const String& ssid);

    // Update network info after scanning/connecting
    void updateRSSI(const String& ssid, int8_t rssi);
    void updateLastConnected(const String& ssid, uint32_t timestamp);

    // Persistence
    bool load();
    bool save();

    // Get sorted list by priority and signal strength
    std::vector<NetworkCredential*> getSortedNetworks();

private:
    std::vector<NetworkCredential> credentials;
    Preferences prefs;

    static const char* PREF_NAMESPACE;
    static const char* PREF_COUNT_KEY;
    static const int MAX_NETWORKS = 10;

    String getSSIDKey(int index) const;
    String getPasswordKey(int index) const;
    String getPriorityKey(int index) const;
    String getRSSIKey(int index) const;
    String getLastConnectedKey(int index) const;
};

} // namespace WiFiMan

#endif // WIFIMAN_CREDENTIALS_H
