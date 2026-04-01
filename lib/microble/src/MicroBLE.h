#pragma once

#ifndef NATIVE_TEST
#include <NimBLEDevice.h>
#endif

namespace MicroBLE {

// Initialize the NimBLE stack. Idempotent — safe to call multiple times.
// Must be called before any BleGattService::begin() or BLE client operations.
void init(const char* deviceName, int8_t power = 9);

bool isInitialized();

#ifndef NATIVE_TEST
// Shared NimBLE server instance (created on first init)
NimBLEServer* server();

// Direct access to advertising for protocol-specific configuration
NimBLEAdvertising* advertising();
#endif

void startAdvertising();
void stopAdvertising();

// --- Internal: used by BleGattService to register for server callbacks ---
class BleGattService;  // forward declare
void registerService(BleGattService* svc);

} // namespace MicroBLE
