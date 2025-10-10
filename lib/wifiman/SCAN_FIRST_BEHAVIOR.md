# WiFiMan Scan-First Behavior

## Overview

WiFiMan always scans for available networks before attempting any connections. This ensures efficient use of time and resources by only trying to connect to networks that are actually in range.

## Connection Flow

```
┌─────────────┐
│   begin()   │
└──────┬──────┘
       │
       ├─── Has saved credentials?
       │
      YES ───────────────────────────► SCANNING
       │                                   │
       │                                   │ Async WiFi.scanNetworks()
       │                                   │
       │                                   ▼
       │                              Scan Complete
       │                                   │
       │                                   │ Filter: Only saved networks
       │                                   │         that were found in scan
       │                                   │
       │                                   ▼
       │                              availableNetworks[]
       │                              (sorted by priority, RSSI, history)
       │                                   │
       │                                   ├─── Any available?
       │                                   │
       │                                  YES ───► CONNECTING
       │                                   │            │
       │                                   │            │ Try network 1
       │                                   │            │ Try network 2
       │                                   │            │ Try network 3...
       │                                   │            │
       │                                   │           SUCCESS ───► CONNECTED
       │                                   │            │                │
       │                                   │           FAIL              │
       │                                   │            │                │
       │                                   NO           │          Connection lost?
       │                                   │            │                │
       │                                   ▼            ▼                ▼
       │                              FAILED ◄──────────┴──────► SCANNING (retry)
       │                                   │
       │                              (wait retryDelay)
       │                                   │
       │                                   └──────► SCANNING (retry)
       │
      NO
       │
       ▼
   AP_MODE (captive portal for configuration)
```

## Key Behaviors

### 1. Always Scan First
- **Every connection attempt** starts with a scan
- No blind connection attempts to potentially unavailable networks
- Scan timeout: 10 seconds

### 2. Filter by Availability
```cpp
// Pseudo-code of what happens:
scanResults = WiFi.scanNetworks();
availableNetworks = [];

for (savedNetwork : allSavedNetworks) {
    if (scanResults.contains(savedNetwork.ssid)) {
        availableNetworks.add(savedNetwork);
    }
}

// Only try networks in availableNetworks
```

### 3. Smart Sorting
Available networks are sorted by:
1. **Priority** (highest first) - User-defined importance
2. **Last Connected** (most recent first) - Successfully connected networks
3. **RSSI** (strongest first) - Signal strength from scan

### 4. Efficient Connection Attempts
```cpp
// Example scenario:
// Saved networks:
//   - HomeWiFi (priority: 100)
//   - WorkWiFi (priority: 50)
//   - Hotspot  (priority: 10)
//
// Scan results:
//   - RandomNet (-60 dBm)
//   - WorkWiFi  (-55 dBm)
//   - Hotspot   (-80 dBm)
//
// Available networks (after filtering):
//   - WorkWiFi  (priority: 50, RSSI: -55 dBm)
//   - Hotspot   (priority: 10, RSSI: -80 dBm)
//
// Connection order:
//   1. Try WorkWiFi  (higher priority, better signal)
//   2. Try Hotspot   (if WorkWiFi fails)
//
// HomeWiFi is NOT attempted because it wasn't found in the scan
```

### 5. Re-scan on Disconnect
When an active connection is lost, WiFiMan:
1. Calls `onDisconnected()` callback
2. Transitions to **SCANNING** (not directly to CONNECTING)
3. Performs a fresh scan to find currently available networks
4. Attempts reconnection using the updated availability list

## Serial Output Example

```
[WiFiMan] Starting WiFi Manager
[WiFiMan] Found 3 saved network(s), starting scan
[WiFiMan] State transition: IDLE -> SCANNING
[WiFiMan] Starting network scan
[WiFiMan] Scan found 5 networks
[WiFiMan]   - WorkWiFi (RSSI: -55) [SAVED]
[WiFiMan]   - Hotspot (RSSI: -80) [SAVED]
[WiFiMan] 2 saved network(s) are currently available
[WiFiMan]   -> WorkWiFi (Priority: 50, RSSI: -55 dBm)
[WiFiMan]   -> Hotspot (Priority: 10, RSSI: -80 dBm)
[WiFiMan] State transition: SCANNING -> CONNECTING
[WiFiMan] Attempting connection to 'WorkWiFi' (priority: 50, RSSI: -55)
[WiFiMan] WiFi connected event
[WiFiMan] Got IP event
[WiFiMan] State transition: CONNECTING -> CONNECTED
[WiFiMan] Connected to 'WorkWiFi', IP: 192.168.1.100
```

## Advantages

### ✅ No Wasted Time
- Doesn't try to connect to out-of-range networks
- No long timeouts waiting for impossible connections

### ✅ Better User Experience
- Faster initial connection
- Smarter network selection based on actual availability

### ✅ Network Mobility
- Device can move between locations (home/work/etc)
- Automatically connects to best available network at current location

### ✅ Updated Signal Strength
- RSSI values are always fresh from the scan
- Better decision making based on current conditions

## Configuration

The scan-first behavior is **always active** and cannot be disabled. This is by design to ensure optimal performance.

However, you can tune the behavior:

```cpp
// How long to wait when connecting to each available network
wifiManager.setConnectionTimeout(15000);  // default: 15s

// How long to wait before retrying after all networks fail
wifiManager.setRetryDelay(5000);  // default: 5s
```

## Edge Cases

### No Credentials Saved
```
begin() -> AP_MODE (immediate, no scan)
```

### All Scans Timeout
```
SCANNING -> (timeout) -> FAILED -> (retry after retryDelay) -> SCANNING
```

### No Saved Networks Found in Scan
```
SCANNING -> (0 available) -> FAILED -> (retry after retryDelay) -> SCANNING
```

### All Connection Attempts Fail
```
CONNECTING -> (all failed) -> FAILED -> (retry after retryDelay) -> SCANNING
```

## API Impact

### Removed Methods
- ~~`setScanBeforeConnect(bool)`~~ - Scanning is always enabled

### New Internal State
- `availableNetworks` - Filtered list of networks found in scan
- Only these networks are attempted during connection

### Unchanged API
All other methods work exactly as before. The scan-first behavior is transparent to the user.
