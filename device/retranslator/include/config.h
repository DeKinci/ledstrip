#pragma once

// --- Device Identity ---
// Each device MUST have a unique ID. Abonents: 0x01-0x7F, Retranslators: 0x80-0xFE
#ifndef DEVICE_ID
#define DEVICE_ID 0x01
#endif

// --- Node Type (0 = retranslator, 1 = abonent) ---
#ifndef NODE_TYPE
#define NODE_TYPE 0
#endif

// --- E220 LoRa Module Pins (UART) ---
#ifndef LORA_TX_PIN
#define LORA_TX_PIN 43
#endif

#ifndef LORA_RX_PIN
#define LORA_RX_PIN 44
#endif

#ifndef LORA_M0_PIN
#define LORA_M0_PIN 1
#endif

#ifndef LORA_M1_PIN
#define LORA_M1_PIN 2
#endif

#ifndef LORA_AUX_PIN
#define LORA_AUX_PIN 3
#endif

// --- LoRa UART ---
#ifndef LORA_BAUD_RATE
#define LORA_BAUD_RATE 9600
#endif

#ifndef LORA_SERIAL_NUM
#define LORA_SERIAL_NUM 1
#endif

// --- Message & Storage Limits ---
#ifndef MAX_MSG_PAYLOAD
#define MAX_MSG_PAYLOAD 100
#endif

#ifndef MAX_SENDERS
#define MAX_SENDERS 8
#endif

#ifndef MSGS_PER_SENDER
#define MSGS_PER_SENDER 64
#endif

// --- Sync Timing ---
#ifndef BEACON_INTERVAL_MS
#define BEACON_INTERVAL_MS 10000
#endif

// --- Presence Thresholds ---
#ifndef PRESENCE_STALE_MS
#define PRESENCE_STALE_MS 120000    // 2 minutes
#endif

#ifndef PRESENCE_TIMEOUT_MS
#define PRESENCE_TIMEOUT_MS 600000  // 10 minutes
#endif

#ifndef PRESENCE_EXPIRE_MS
#define PRESENCE_EXPIRE_MS 3600000  // 1 hour — fully purge sender from digest/hash
#endif

// --- Sync Session ---
#ifndef SYNC_TIMEOUT_MS
#define SYNC_TIMEOUT_MS 30000       // 30 seconds without activity
#endif

// --- BLE UUIDs ---
#define BLE_SERVICE_UUID        "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_RX_CHARACTERISTIC   "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_TX_CHARACTERISTIC   "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

// --- BLE Device Name ---
#ifndef BLE_DEVICE_NAME
#define BLE_DEVICE_NAME "Retranslator"
#endif
