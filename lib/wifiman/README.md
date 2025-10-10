# WiFiMan - Advanced WiFi Manager for ESP32

A robust, non-blocking WiFi manager with support for multiple saved networks, captive portal configuration, and smart connection management.

## Features

- **Multiple Network Support**: Save and manage multiple WiFi credentials with priority levels
- **Non-Blocking Operation**: Never blocks your main loop - continues operation during connection attempts
- **Smart Network Scanning**: Always scans for available networks before connecting - only attempts connection to networks that are actually in range
- **Smart Connection**: Automatically connects to the best available network based on priority, signal strength, and connection history
- **Captive Portal**: Beautiful web-based configuration interface when in AP mode
- **Persistent Storage**: Credentials saved to NVS (Non-Volatile Storage)
- **Auto-Retry**: Intelligent retry logic with configurable delays
- **Status Callbacks**: Hook into connection events for custom behavior

## Installation

This library is designed for PlatformIO. It's already included in the `lib/` folder.

### Dependencies

- ArduinoJson (^7.0.0)
- ESPAsyncWebServer
- AsyncTCP

## Quick Start

### Basic Usage

```cpp
#include <WiFiMan.h>
#include <ESPAsyncWebServer.h>

AsyncWebServer server(80);
WiFiMan::WiFiManager wifiManager(&server);

void setup() {
    Serial.begin(115200);

    // Configure AP credentials (used when no networks available)
    wifiManager.setAPCredentials("ESP32-Setup", "");

    // Set hostname
    wifiManager.setHostname("mydevice");

    // Optional: Add network programmatically
    wifiManager.credentials().addNetwork("MyWiFi", "mypassword", 100);

    // Start the manager
    wifiManager.begin();

    // Start your web server
    server.begin();
}

void loop() {
    // MUST call this regularly
    wifiManager.loop();

    // Your application code here
}
```

### With Callbacks

```cpp
void setup() {
    Serial.begin(115200);

    wifiManager.setAPCredentials("ESP32-Setup");

    // Called when connected to WiFi
    wifiManager.onConnected([](const String& ssid) {
        Serial.printf("Connected to %s\n", ssid.c_str());
        Serial.printf("IP: %s\n", wifiManager.getIP().toString().c_str());
        // Start your services here
    });

    // Called when disconnected
    wifiManager.onDisconnected([]() {
        Serial.println("WiFi disconnected");
        // Stop services or handle reconnection
    });

    // Called when AP mode starts
    wifiManager.onAPStarted([](const String& ssid) {
        Serial.printf("AP Mode started: %s\n", ssid.c_str());
        Serial.printf("Configure at: http://%s\n",
                     wifiManager.getIP().toString().c_str());
    });

    wifiManager.begin();
    server.begin();
}
```

## Configuration Options

### Connection Settings

```cpp
// Set connection timeout (default: 15000ms)
wifiManager.setConnectionTimeout(20000);

// Set retry delay after failed connection (default: 5000ms)
wifiManager.setRetryDelay(10000);

// Set AP timeout - auto-connect after timeout (0 = never, default: 0)
wifiManager.setAPTimeout(300000);  // 5 minutes
```

### Managing Credentials

```cpp
WiFiMan::WiFiCredentials& creds = wifiManager.credentials();

// Add network with priority (higher = preferred)
creds.addNetwork("HomeWiFi", "password123", 100);
creds.addNetwork("WorkWiFi", "workpass", 50);

// Remove network
creds.removeNetwork("OldWiFi");

// Update priority
creds.updatePriority("HomeWiFi", 200);

// Clear all saved networks
creds.clearAll();

// Check if network exists
if (creds.hasNetwork("MyWiFi")) {
    // ...
}

// Get all networks
const std::vector<WiFiMan::NetworkCredential>& networks = creds.getAll();
for (const auto& net : networks) {
    Serial.printf("SSID: %s, Priority: %d\n", net.ssid.c_str(), net.priority);
}
```

## States

The WiFi manager operates in several states:

- **IDLE**: Not actively doing anything
- **SCANNING**: Scanning for available networks
- **CONNECTING**: Attempting to connect to a network
- **CONNECTED**: Successfully connected to WiFi
- **AP_MODE**: Running as Access Point (configuration mode)
- **FAILED**: Connection failed, will retry

```cpp
WiFiMan::State state = wifiManager.getState();
String stateStr = wifiManager.getStateString();

if (wifiManager.isConnected()) {
    // Do something
}

if (wifiManager.isAPMode()) {
    // Show configuration prompt
}
```

## Captive Portal

When the WiFi manager enters AP mode (either because no credentials are saved, or all connection attempts failed), it starts a captive portal that allows users to:

1. Scan for available networks
2. Add new WiFi credentials with priorities
3. View and manage saved networks
4. Remove networks
5. Trigger immediate connection attempts

The captive portal is accessible at:
- http://192.168.4.1/ (default AP IP)
- Most devices will auto-redirect when connecting to the AP

### Captive Portal Endpoints

The following REST API endpoints are available:

- `GET /wifiman/scan` - Scan for networks
- `GET /wifiman/list` - List saved networks
- `POST /wifiman/add` - Add network (JSON: {ssid, password, priority})
- `POST /wifiman/remove` - Remove network (JSON: {ssid})
- `POST /wifiman/clear` - Clear all networks
- `POST /wifiman/connect` - Trigger connection attempt
- `GET /wifiman/status` - Get current status

## Advanced Usage

### Manual Control

```cpp
// Force start AP mode
wifiManager.startAP();

// Stop AP mode
wifiManager.stopAP();

// Disconnect from current network
wifiManager.disconnect();

// Retry connection to saved networks
wifiManager.retry();
```

### Integration with Existing Web Server

If you already have a web server with routes, WiFiMan will only add its routes when in AP mode. Your existing routes work normally when connected to WiFi.

```cpp
AsyncWebServer server(80);
WiFiMan::WiFiManager wifiManager(&server);

void setup() {
    // Add your routes
    server.on("/api/data", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    // Start WiFiMan (will add captive portal routes when in AP mode)
    wifiManager.begin();

    // Start server
    server.begin();
}
```

## Non-Blocking Behavior

WiFiMan is **completely non-blocking** and never calls `delay()`. This means:

- Network scanning happens asynchronously via `WiFi.scanNetworks(true)`
- Connection attempts use non-blocking `WiFi.begin()` and `WiFi.disconnect(false, false)`
- **Only networks that were found in the scan will be attempted** - no wasted time
- All timing uses `millis()` comparisons, never `delay()`
- State transitions are immediate and handled in `loop()`
- Typical execution time per `loop()` call: **< 1ms**
- Your application continues to run at full speed during all WiFi operations

**Important**: You MUST call `wifiManager.loop()` regularly (every iteration of your main loop) for the manager to function properly.

## How Connection Works

WiFiMan uses a smart scan-first approach:

1. **SCANNING**: Performs async network scan to find what's actually available
2. **Filtering**: Only considers saved networks that were found in the scan
3. **Sorting**: Sorts available networks by priority → last connected → signal strength
4. **CONNECTING**: Attempts connection only to available networks in sorted order
5. **Auto-Retry**: If connection lost or all attempts fail, re-scans and tries again

This approach ensures you never waste time trying to connect to networks that aren't in range.

## Persistence

All credentials are automatically saved to ESP32's NVS (Non-Volatile Storage) and persist across reboots. The library stores:

- Network SSIDs and passwords
- Priority levels
- Last known RSSI (signal strength)
- Last successful connection timestamp

## Migration from NetWizard

If you're migrating from NetWizard:

```cpp
// Before (NetWizard)
DNSServer dnsServer;
AsyncWebServer server(80);
NetWizard NW(&server);

void setup() {
    NW.setStrategy(NetWizardStrategy::NON_BLOCKING);
    NW.autoConnect("LED", "");
    NW.loop();
}

// After (WiFiMan)
AsyncWebServer server(80);
WiFiMan::WiFiManager wifiManager(&server);

void setup() {
    wifiManager.setAPCredentials("LED", "");
    wifiManager.begin();
    server.begin();
}

void loop() {
    wifiManager.loop();  // Instead of NW.loop()
}
```

## Example Project Structure

```
src/
  main.cpp
lib/
  wifiman/
    WiFiMan.h
    WiFiMan.cpp
    WiFiCredentials.h
    WiFiCredentials.cpp
    WiFiManAPI.cpp
    WiFiManWebUI.h
    library.json
    README.md
```

## Troubleshooting

### WiFi not connecting

1. Check credentials are correct
2. Verify network is in range
3. Check serial output for connection attempts
4. Ensure `loop()` is being called

### Captive portal not appearing

1. Verify you're connected to the ESP32's AP
2. Try navigating to http://192.168.4.1/
3. Check that the web server was passed to WiFiMan constructor
4. Some devices may require disabling mobile data

### Memory issues

The web UI is stored in PROGMEM to minimize RAM usage. If you experience memory issues, you can:

1. Reduce the number of saved networks (MAX_NETWORKS in WiFiCredentials.h)
2. Increase the connection timeout to allow more time per network

## License

Part of the SmartGarland project.
