#include "WiFiMan.h"
#include <Logger.h>

static const char* TAG = "WiFiMan";

namespace WiFiMan {

WiFiManager* WiFiManager::instance = nullptr;

WiFiManager::WiFiManager(HttpDispatcher* dispatcher)
    : _dispatcher(dispatcher),
      dnsServer(nullptr),
      state(State::IDLE),
      apSSID("ESP32-Setup"),
      apPassword(""),
      hostname("esp32"),
      connectionTimeout(15000),
      retryDelay(5000),
      apTimeout(0),
      stateStartTime(0),
      lastConnectionAttempt(0),
      apStartTime(0),
      webConnectRequestTime(0),
      currentNetworkIndex(-1),
      consecutiveFailures(0),
      _captiveRootHandle(HttpDispatcher::RouteHandle::invalid()),
      _captiveDetectCount(0) {

    instance = this;
    creds.load();
}

WiFiManager::~WiFiManager() {
    if (dnsServer) {
        delete dnsServer;
    }
    instance = nullptr;
}

void WiFiManager::setAPCredentials(const String& ssid, const String& password) {
    apSSID = ssid;
    apPassword = password;
}

void WiFiManager::setConnectionTimeout(uint32_t timeoutMs) {
    connectionTimeout = timeoutMs;
}

void WiFiManager::setRetryDelay(uint32_t delayMs) {
    retryDelay = delayMs;
}

void WiFiManager::setAPTimeout(uint32_t timeoutMs) {
    apTimeout = timeoutMs;
}

void WiFiManager::setHostname(const String& name) {
    hostname = name;
}

void WiFiManager::begin() {
    LOG_INFO(TAG, "Starting WiFi Manager");
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(hostname.c_str());

    // Register WiFi event handler with info for disconnect reason
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        if (instance) {
            instance->handleWiFiEvent(event, info);
        }
    });

    // Setup permanent web routes at /wifiman (available in both AP and STA modes)
    setupRoutes();

    if (creds.getAll().empty()) {
        LOG_INFO(TAG, "No credentials stored, starting AP mode");
        transitionToState(State::AP_MODE);
    } else {
        LOG_INFO(TAG, "Found %d saved network(s), starting scan", creds.getAll().size());
        transitionToState(State::SCANNING);
    }
}

void WiFiManager::disconnect() {
    WiFi.disconnect();
    transitionToState(State::IDLE);
}

void WiFiManager::startAP() {
    transitionToState(State::AP_MODE);
}

void WiFiManager::stopAP() {
    if (state == State::AP_MODE) {
        WiFi.softAPdisconnect(true);
        teardownCaptivePortal();
        if (dnsServer) {
            dnsServer->stop();
        }
        transitionToState(State::IDLE);
    }
}

void WiFiManager::retry() {
    if (state == State::FAILED || state == State::IDLE) {
        lastConnectionAttempt = 0;
        transitionToState(State::SCANNING);
    }
}

void WiFiManager::loop() {
    switch (state) {
        case State::IDLE:
            handleIdle();
            break;
        case State::SCANNING:
            handleScanning();
            break;
        case State::CONNECTING:
            handleConnecting();
            break;
        case State::CONNECTED:
            handleConnected();
            break;
        case State::AP_MODE:
            handleAPMode();
            break;
        case State::FAILED:
            handleFailed();
            break;
    }

    // Process DNS for captive portal
    if (dnsServer) {
        dnsServer->processNextRequest();
    }
}

String WiFiManager::getStateString() const {
    switch (state) {
        case State::IDLE: return "IDLE";
        case State::SCANNING: return "SCANNING";
        case State::CONNECTING: return "CONNECTING";
        case State::CONNECTED: return "CONNECTED";
        case State::AP_MODE: return "AP_MODE";
        case State::FAILED: return "FAILED";
        default: return "UNKNOWN";
    }
}

String WiFiManager::getCurrentSSID() const {
    if (state == State::CONNECTED) {
        return WiFi.SSID();
    } else if (state == State::AP_MODE) {
        return apSSID;
    }
    return "";
}

IPAddress WiFiManager::getIP() const {
    if (state == State::CONNECTED) {
        return WiFi.localIP();
    } else if (state == State::AP_MODE) {
        return WiFi.softAPIP();
    }
    return IPAddress(0, 0, 0, 0);
}

void WiFiManager::onConnected(std::function<void(const String& ssid)> callback) {
    connectedCallback = callback;
}

void WiFiManager::onDisconnected(std::function<void()> callback) {
    disconnectedCallback = callback;
}

void WiFiManager::onAPStarted(std::function<void(const String& ssid)> callback) {
    apStartedCallback = callback;
}

void WiFiManager::onAPClientConnected(std::function<void(uint8_t numClients)> callback) {
    apClientConnectedCallback = callback;
}

void WiFiManager::handleIdle() {
    // Do nothing, wait for user action
}

void WiFiManager::handleScanning() {
    // Scanning is async, wait for completion or timeout
    if (WiFi.scanComplete() >= 0) {
        scanAvailableNetworks();

        // Only proceed to CONNECTING if we found available networks
        if (!availableNetworks.empty()) {
            transitionToState(State::CONNECTING);
        } else {
            LOG_INFO(TAG, "No saved networks are available");
            transitionToState(State::FAILED);
        }
    } else if (millis() - stateStartTime > 10000) {
        LOG_WARN(TAG, "Scan timeout");
        transitionToState(State::FAILED);
    }
}

void WiFiManager::handleConnecting() {
    // Check if connection succeeded
    if (WiFi.status() == WL_CONNECTED) {
        transitionToState(State::CONNECTED);
        return;
    }

    // Check for timeout
    if (millis() - stateStartTime > connectionTimeout) {
        LOG_WARN(TAG, "Connection timeout");
        ConnectionResult result = tryNextNetwork();

        if (result == ConnectionResult::NO_NETWORKS_AVAILABLE ||
            result == ConnectionResult::NO_CREDENTIALS) {
            transitionToState(State::FAILED);
        }
        // If result is IN_PROGRESS, we stay in CONNECTING state
    }
}

void WiFiManager::handleConnected() {
    // Monitor connection health
    if (WiFi.status() != WL_CONNECTED) {
        LOG_WARN(TAG, "Connection lost, rescanning");
        if (disconnectedCallback) {
            disconnectedCallback();
        }
        // Re-scan to find available networks
        transitionToState(State::SCANNING);
    }
}

void WiFiManager::handleAPMode() {
    updateAPClients();

    // Check for web-initiated connection request
    if (webConnectRequestTime > 0 && millis() - webConnectRequestTime > 100) {
        webConnectRequestTime = 0;
        consecutiveFailures = 0;  // Reset failure counter on manual retry
        LOG_INFO(TAG, "Processing web connect request");
        stopAP();
        retry();
        return;
    }

    // Check for AP timeout
    if (apTimeout > 0 && millis() - apStartTime > apTimeout) {
        LOG_INFO(TAG, "AP timeout, retrying connection");
        stopAP();
        retry();
    }
}

void WiFiManager::handleFailed() {
    // Auto-retry after delay, but with a limit
    if (millis() - lastConnectionAttempt > retryDelay) {
        consecutiveFailures++;

        if (consecutiveFailures >= 3) {
            LOG_WARN(TAG, "Too many failures, starting AP mode");
            consecutiveFailures = 0;
            transitionToState(State::AP_MODE);
        } else {
            LOG_INFO(TAG, "Retrying connection after failure (attempt %d/3)", consecutiveFailures + 1);
            retry();
        }
    }
}

void WiFiManager::startScanning() {
    LOG_INFO(TAG, "Starting network scan");
    WiFi.scanNetworks(true);  // Async scan
}

void WiFiManager::startConnecting() {
    // Use availableNetworks (already filtered and sorted) instead of all networks
    sortedNetworks = availableNetworks;
    currentNetworkIndex = -1;
    tryNextNetwork();
}

ConnectionResult WiFiManager::tryNextNetwork() {
    currentNetworkIndex++;

    if (sortedNetworks.empty() || currentNetworkIndex >= sortedNetworks.size()) {
        LOG_INFO(TAG, "No more networks to try");
        return sortedNetworks.empty() ? ConnectionResult::NO_CREDENTIALS : ConnectionResult::NO_NETWORKS_AVAILABLE;
    }

    NetworkCredential* cred = sortedNetworks[currentNetworkIndex];
    LOG_INFO(TAG, "Attempting connection to '%s' (priority: %d, RSSI: %d)",
             cred->ssid.c_str(), cred->priority, cred->lastRSSI);

    // Non-blocking disconnect and connect
    WiFi.disconnect(false, false);  // Don't erase config, don't wait
    WiFi.begin(cred->ssid.c_str(), cred->password.c_str());

    stateStartTime = millis();
    return ConnectionResult::IN_PROGRESS;
}

void WiFiManager::scanAvailableNetworks() {
    int n = WiFi.scanComplete();
    if (n < 0) return;

    LOG_INFO(TAG, "Scan found %d networks", n);

    // Clear previous available networks
    availableNetworks.clear();

    // Print all scanned networks
    for (int i = 0; i < n; i++) {
        LOG_INFO(TAG, "  Scanned: '%s' (RSSI: %d)", WiFi.SSID(i).c_str(), WiFi.RSSI(i));
    }

    // Print all saved networks
    auto allSaved = creds.getAll();
    LOG_INFO(TAG, "Have %d saved network(s):", allSaved.size());
    for (const auto& saved : allSaved) {
        LOG_INFO(TAG, "  Saved: '%s'", saved.ssid.c_str());
    }

    // Build list of scanned SSIDs for quick lookup
    std::vector<String> scannedSSIDs;
    for (int i = 0; i < n; i++) {
        scannedSSIDs.push_back(WiFi.SSID(i));
    }

    // Update RSSI and build available networks list
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        int8_t rssi = WiFi.RSSI(i);

        if (creds.hasNetwork(ssid)) {
            creds.updateRSSI(ssid, rssi);
            LOG_INFO(TAG, "  Match found: %s (RSSI: %d)", ssid.c_str(), rssi);
        }
    }

    // Get only networks that are both saved AND currently available
    auto sortedSaved = creds.getSortedNetworks();
    for (auto* net : sortedSaved) {
        // Check if this saved network was found in the scan
        bool found = false;
        for (const auto& scannedSSID : scannedSSIDs) {
            if (scannedSSID == net->ssid) {
                found = true;
                break;
            }
        }

        if (found) {
            availableNetworks.push_back(net);
        }
    }

    LOG_INFO(TAG, "%d saved network(s) are currently available", availableNetworks.size());

    for (const auto* net : availableNetworks) {
        LOG_INFO(TAG, "  -> %s (Priority: %d, RSSI: %d dBm)",
                 net->ssid.c_str(), net->priority, net->lastRSSI);
    }

    WiFi.scanDelete();
}

void WiFiManager::transitionToState(State newState) {
    if (state == newState) return;

    LOG_INFO(TAG, "State transition: %s -> %s",
             getStateString().c_str(),
             newState == State::IDLE ? "IDLE" :
             newState == State::SCANNING ? "SCANNING" :
             newState == State::CONNECTING ? "CONNECTING" :
             newState == State::CONNECTED ? "CONNECTED" :
             newState == State::AP_MODE ? "AP_MODE" : "FAILED");

    State oldState = state;
    state = newState;
    stateStartTime = millis();

    switch (newState) {
        case State::SCANNING:
            startScanning();
            break;

        case State::CONNECTING:
            startConnecting();
            break;

        case State::CONNECTED:
            LOG_INFO(TAG, "Connected to '%s', IP: %s",
                     WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
            creds.updateLastConnected(WiFi.SSID(), millis());
            consecutiveFailures = 0;  // Reset failure counter on success
            if (connectedCallback) {
                connectedCallback(WiFi.SSID());
            }
            break;

        case State::AP_MODE:
            WiFi.mode(WIFI_AP_STA);  // Use AP_STA to allow scanning while AP is running
            WiFi.softAP(apSSID.c_str(), apPassword.c_str());
            LOG_INFO(TAG, "AP started: %s, IP: %s",
                     apSSID.c_str(), WiFi.softAPIP().toString().c_str());

            // Setup captive portal DNS
            if (!dnsServer) {
                dnsServer = new DNSServer();
            }
            dnsServer->start(53, "*", WiFi.softAPIP());

            // Setup captive portal routes (high priority "/" override)
            setupCaptivePortal();

            apStartTime = millis();

            if (apStartedCallback) {
                apStartedCallback(apSSID);
            }
            break;

        case State::FAILED:
            LOG_WARN(TAG, "Failed to connect, will retry or start AP");
            lastConnectionAttempt = millis();

            // If we've never been connected and have no other options, start AP
            if (creds.getAll().empty()) {
                transitionToState(State::AP_MODE);
            }
            break;

        default:
            break;
    }
}

void WiFiManager::updateAPClients() {
    static uint8_t lastClientCount = 0;
    uint8_t clientCount = WiFi.softAPgetStationNum();

    if (clientCount != lastClientCount) {
        LOG_INFO(TAG, "AP clients: %d", clientCount);
        lastClientCount = clientCount;
        if (apClientConnectedCallback) {
            apClientConnectedCallback(clientCount);
        }
    }
}

void WiFiManager::handleWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            LOG_INFO(TAG, "WiFi connected event");
            _lastError = "";  // Clear error on successful connection
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
            uint8_t reason = info.wifi_sta_disconnected.reason;
            _lastError = reasonToString(reason);
            // Only log when actively trying to connect — suppress background STA noise in AP mode
            if (state == State::CONNECTING || state == State::CONNECTED || state == State::SCANNING) {
                LOG_WARN(TAG, "WiFi disconnected, reason: %s (%d)", _lastError.c_str(), reason);
            }
            break;
        }
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            LOG_INFO(TAG, "Got IP event");
            _lastError = "";  // Clear error on getting IP
            break;
        default:
            break;
    }
}

String WiFiManager::reasonToString(uint8_t reason) {
    // User-friendly messages for common errors
    switch (reason) {
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_AUTH_EXPIRE:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_MIC_FAILURE:
            return "Wrong password";

        case WIFI_REASON_NO_AP_FOUND:
            return "Network not found";

        case WIFI_REASON_ASSOC_TOOMANY:
            return "Too many clients";

        case WIFI_REASON_BEACON_TIMEOUT:
        case WIFI_REASON_AP_TSF_RESET:
            return "Connection lost";

        // For other reasons, just show the code name
        case WIFI_REASON_UNSPECIFIED: return "UNSPECIFIED";
        case WIFI_REASON_ASSOC_EXPIRE: return "ASSOC_EXPIRE";
        case WIFI_REASON_ASSOC_LEAVE: return "ASSOC_LEAVE";
        case WIFI_REASON_ASSOC_NOT_AUTHED: return "ASSOC_NOT_AUTHED";
        case WIFI_REASON_DISASSOC_PWRCAP_BAD: return "DISASSOC_PWRCAP_BAD";
        case WIFI_REASON_DISASSOC_SUPCHAN_BAD: return "DISASSOC_SUPCHAN_BAD";
        case WIFI_REASON_IE_INVALID: return "IE_INVALID";
        case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT: return "GROUP_KEY_TIMEOUT";
        case WIFI_REASON_IE_IN_4WAY_DIFFERS: return "IE_IN_4WAY_DIFFERS";
        case WIFI_REASON_GROUP_CIPHER_INVALID: return "GROUP_CIPHER_INVALID";
        case WIFI_REASON_PAIRWISE_CIPHER_INVALID: return "PAIRWISE_CIPHER_INVALID";
        case WIFI_REASON_AKMP_INVALID: return "AKMP_INVALID";
        case WIFI_REASON_UNSUPP_RSN_IE_VERSION: return "UNSUPP_RSN_IE_VERSION";
        case WIFI_REASON_INVALID_RSN_IE_CAP: return "INVALID_RSN_IE_CAP";
        case WIFI_REASON_802_1X_AUTH_FAILED: return "802_1X_AUTH_FAILED";
        case WIFI_REASON_CIPHER_SUITE_REJECTED: return "CIPHER_SUITE_REJECTED";
        case WIFI_REASON_CONNECTION_FAIL: return "CONNECTION_FAIL";

        default: {
            String result = "Error ";
            result += reason;
            return result;
        }
    }
}

} // namespace WiFiMan
