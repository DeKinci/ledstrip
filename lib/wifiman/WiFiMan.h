#ifndef WIFIMAN_H
#define WIFIMAN_H

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include "WiFiCredentials.h"

namespace WiFiMan {

enum class State {
    IDLE,
    SCANNING,
    CONNECTING,
    CONNECTED,
    AP_MODE,
    FAILED
};

enum class ConnectionResult {
    SUCCESS,
    FAILED,
    IN_PROGRESS,
    NO_CREDENTIALS,
    NO_NETWORKS_AVAILABLE
};

class WiFiManager {
public:
    WiFiManager(AsyncWebServer* server = nullptr);
    ~WiFiManager();

    // Configuration
    void setAPCredentials(const String& ssid, const String& password = "");
    void setConnectionTimeout(uint32_t timeoutMs);
    void setRetryDelay(uint32_t delayMs);
    void setAPTimeout(uint32_t timeoutMs);  // 0 = never timeout
    void setHostname(const String& hostname);

    // Credentials management
    WiFiCredentials& credentials() { return creds; }

    // Main loop - MUST be called regularly
    void loop();

    // Connection control
    void begin();                           // Start WiFi manager (non-blocking)
    void disconnect();
    void startAP();
    void stopAP();
    void retry();                          // Retry connection to saved networks

    // Status
    State getState() const { return state; }
    bool isConnected() const { return state == State::CONNECTED; }
    bool isAPMode() const { return state == State::AP_MODE; }
    String getStateString() const;
    String getCurrentSSID() const;
    IPAddress getIP() const;

    // Callbacks
    void onConnected(std::function<void(const String& ssid)> callback);
    void onDisconnected(std::function<void()> callback);
    void onAPStarted(std::function<void(const String& ssid)> callback);
    void onAPClientConnected(std::function<void(uint8_t numClients)> callback);

private:
    WiFiCredentials creds;
    AsyncWebServer* webServer;
    DNSServer* dnsServer;

    State state;
    String apSSID;
    String apPassword;
    String hostname;

    uint32_t connectionTimeout;
    uint32_t retryDelay;
    uint32_t apTimeout;

    uint32_t stateStartTime;
    uint32_t lastConnectionAttempt;
    uint32_t apStartTime;
    uint32_t webConnectRequestTime;
    int currentNetworkIndex;
    int consecutiveFailures;  // Track consecutive connection failures
    std::vector<NetworkCredential*> sortedNetworks;
    std::vector<NetworkCredential*> availableNetworks;  // Networks found in last scan

    // Callbacks
    std::function<void(const String&)> connectedCallback;
    std::function<void()> disconnectedCallback;
    std::function<void(const String&)> apStartedCallback;
    std::function<void(uint8_t)> apClientConnectedCallback;

    // Internal state machine
    void handleIdle();
    void handleScanning();
    void handleConnecting();
    void handleConnected();
    void handleAPMode();
    void handleFailed();

    // Helper methods
    void startScanning();
    void startConnecting();
    void updateAPClients();
    ConnectionResult tryNextNetwork();
    void transitionToState(State newState);
    void scanAvailableNetworks();
    void setupWebServer();
    void teardownWebServer();

    // WiFi event handlers
    static void onWiFiEvent(WiFiEvent_t event);
    void handleWiFiEvent(WiFiEvent_t event);

    static WiFiManager* instance;  // For static event handler
};

} // namespace WiFiMan

#endif // WIFIMAN_H
