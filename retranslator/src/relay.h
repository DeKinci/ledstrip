#pragma once

#include <array>
#include <cstdint>
#include "message.h"
#include "state.h"
#include "lora.h"
#include "ble.h"
#include "ble_cmd.h"
#include "config.h"

#ifndef NATIVE_TEST
#include <Preferences.h>
#endif

class Relay {
public:
    Relay(LoRa& lora, BLE& ble) : _lora(lora), _ble(ble) {}

    void begin();
    void process();

    // Called by BLE callback to queue incoming BLE data
    void onBleReceive(const uint8_t* data, size_t len);

    // Access store (for testing or external use)
    MessageStore& store() { return _store; }

private:
    void pollLoRa();
    void pollBle();
    void sendBeacon();
    void handleLoRaMessage(const uint8_t* buf, size_t len);
    void handleLiveMessage(Message& msg);
    void handleBeacon(const Message& msg);
    void handleDigest(const Message& msg);
    void handleSyncRequest(const Message& msg);
    void continueSyncSend();
    void checkPresence();
    void resetSyncSession();
    void saveSeq();
    void loadSeq();

    // BLE command handlers
    void handleBleCommand(const uint8_t* data, size_t len);
    void bleSetClock(const uint8_t* data, size_t len);
    void bleSetLocation(const uint8_t* data, size_t len);
    void bleSendText(const uint8_t* data, size_t len);
    void bleGetState();
    void bleGetMessages(const uint8_t* data, size_t len);
    void bleGetSelfInfo();

    // Push message to BLE app
    void blePushMessage(const MessageEntry& entry);

    // Get current unix timestamp from clock offset
    uint32_t currentTimestamp() const;

    LoRa& _lora;
    BLE& _ble;
    MessageStore _store;

    // Clock: unix_seconds = millis()/1000 + _clockOffset
    int32_t _clockOffset = 0;

    // Per-sender sequence counter (for messages we originate)
    uint16_t _nextSeq = 1;

    // Boot counter (persisted in NVS)
    uint32_t _bootCount = 0;

    // Beacon timer
    uint32_t _lastBeacon = 0;
    uint16_t _lastSentHash = 0;
    uint16_t _peerHash = 0;

    // Active sync session
    struct SyncSession {
        bool active = false;
        uint8_t peerId = 0;
        uint32_t startMs = 0;
        uint32_t lastActivityMs = 0;
        bool digestSent = false;
        SyncNeeds theyNeed;  // what peer requested from us
        uint8_t sendIndex = 0;   // round-robin index across senders
        uint16_t sendSeqCursor[MAX_SENDERS] = {}; // current seq cursor per sender in theyNeed
    };
    SyncSession _sync;

    // BLE receive buffer (set from callback, consumed in process())
    static constexpr size_t BLE_RX_BUF_SIZE = MAX_MSG_PAYLOAD + MESSAGE_HEADER_SIZE;
    uint8_t _bleRxBuf[BLE_RX_BUF_SIZE] = {};
    size_t _bleRxLen = 0;
    volatile bool _bleRxReady = false;

    // Chunked BLE message streaming (one per loop iteration)
    struct BleGetMsgState {
        bool active = false;
        uint8_t senderId = 0;
        uint16_t currentSeq = 0;
        uint16_t endSeq = 0;
    };
    BleGetMsgState _bleGetMsg;

#ifndef NATIVE_TEST
    Preferences _prefs;
#endif

    // Presence tracking
    struct PresenceState {
        uint8_t senderId = 0;
        Presence last = Presence::Offline;
        bool tracked = false;
    };
    std::array<PresenceState, MAX_SENDERS> _presence = {};
};
