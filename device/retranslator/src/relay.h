#pragma once

#include <array>
#include <cstdint>
#include "message.h"
#include "state.h"
#include "lora.h"
#include "ble_cmd.h"
#include "config.h"

#ifndef NATIVE_TEST
#include <Preferences.h>
#include <BleMessageService.h>
#include <NimBLECharacteristic.h>
#endif

#ifndef PRESENCE_CHECK_INTERVAL_MS
#define PRESENCE_CHECK_INTERVAL_MS 1000
#endif

#ifndef MAX_SPEEDTEST_PINGS
#define MAX_SPEEDTEST_PINGS 256
#endif

#ifndef SPEEDTEST_PONG_TIMEOUT_MS
#define SPEEDTEST_PONG_TIMEOUT_MS 5000
#endif

// --- Stats ---

struct Stats {
    // LoRa
    uint32_t packets_rx      = 0;
    uint32_t packets_tx      = 0;
    uint32_t tx_failures     = 0;
    uint32_t decode_failures = 0;
    // Sync
    uint32_t sync_started    = 0;
    uint32_t sync_completed  = 0;
    uint32_t sync_timeout    = 0;
    // Messages
    uint32_t msgs_stored     = 0;
    uint32_t dups_rejected   = 0;
    // Beacons
    uint32_t beacons_sent    = 0;
    uint32_t beacons_rx      = 0;
    uint32_t hash_mismatches = 0;
    // System (computed at push time)
    uint32_t uptime_s        = 0;
    uint32_t free_heap       = 0;
    uint32_t loop_time_us    = 0;

    static constexpr size_t FIELD_COUNT = 15;
    static constexpr size_t WIRE_SIZE = FIELD_COUNT * 4;  // 60 bytes
};

class Relay {
public:
    explicit Relay(LoRa& lora) : _lora(lora) {}

    void begin();
    void process();

    MessageStore& store() { return _store; }

private:
    void pollLoRa();
    void sendBeacon();
    void sendOurDigest(uint32_t nowMs);
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
    uint16_t nextSeq();

    // BLE command handlers
    void handleBleCommand(const uint8_t* data, size_t len);
    void bleSetClock(const uint8_t* data, size_t len);
    void bleSetLocation(const uint8_t* data, size_t len);
    void bleSendText(const uint8_t* data, size_t len);
    void bleGetState();
    void bleGetMessages(const uint8_t* data, size_t len);
    void bleGetSelfInfo();
    void bleResetStats();
    void bleStartSpeedtest(const uint8_t* data, size_t len);
    void bleGetSpeedtestResults();

    // Push message to BLE app
    void blePushMessage(const MessageEntry& entry);

    // BLE helpers
    bool bleIsConnected() const;
    bool bleSend(const uint8_t* data, size_t len);

    // Stats
    void pushStats();
    bool loraSend(const uint8_t* data, size_t len);

    // Speedtest
    void handlePing(const Message& msg);
    void speedtestProcess(uint32_t nowMs);
    void speedtestSendPing();
    void speedtestHandlePong(const Message& msg);
    void speedtestComputeResults();

    uint32_t currentTimestamp() const;

    LoRa& _lora;
    MessageStore _store;
    Stats _stats;

    int32_t _clockOffset = 0;
    uint16_t _nextSeq = 1;
    uint32_t _bootCount = 0;
    uint32_t _lastBeacon = 0;
    uint32_t _lastPresenceCheck = 0;
    uint32_t _lastStatsPush = 0;
    uint16_t _lastSentHash = 0;
    uint16_t _peerHash = 0;
    uint32_t _loopMaxUs = 0;

    // Active sync session
    struct SyncSession {
        bool active = false;
        uint8_t peerId = 0;
        uint32_t startMs = 0;
        uint32_t lastActivityMs = 0;
        bool digestSent = false;
        SyncNeeds theyNeed;
        uint8_t sendIndex = 0;
        std::array<uint16_t, MAX_SENDERS> sendSeqCursor = {};
    };
    SyncSession _sync;

    // Chunked BLE message streaming (one per loop iteration)
    struct BleGetMsgState {
        bool active = false;
        uint8_t senderId = 0;
        uint16_t currentSeq = 0;
        uint16_t endSeq = 0;
    };
    BleGetMsgState _bleGetMsg;

    // Speedtest state
    enum class SpeedtestPhase : uint8_t { IDLE, RUNNING, DONE };

    struct SpeedtestResults {
        uint16_t count = 0;
        uint16_t intervalMs = 0;
        uint8_t  payloadSize = 0;
        uint16_t totalSent = 0;
        uint16_t totalReceived = 0;
        uint16_t totalLost = 0;
        uint16_t lossRatePctX10 = 0;
        uint16_t rttMin = 0, rttMax = 0, rttAvg = 0;
        uint16_t rttP1 = 0, rttP5 = 0, rttP10 = 0, rttP25 = 0;
        uint16_t rttP50 = 0, rttP75 = 0, rttP90 = 0, rttP95 = 0, rttP99 = 0;
        uint32_t testDurationMs = 0;
        uint16_t actualIntervalAvgMs = 0;
    };

    struct SpeedtestState {
        SpeedtestPhase phase = SpeedtestPhase::IDLE;
        uint16_t count = 0;
        uint16_t intervalMs = 0;
        uint8_t  payloadSize = 0;
        SpeedtestResults results;
        uint16_t nextPingSeq = 0;
        uint16_t pongCount = 0;
        uint32_t testStartMs = 0;
        uint32_t lastPingSentMs = 0;
        uint32_t lastPingDoneMs = 0;
        uint32_t sendTimeUs[MAX_SPEEDTEST_PINGS] = {};
        uint16_t rttMs[MAX_SPEEDTEST_PINGS] = {};
        uint16_t rttCount = 0;
    };
    SpeedtestState _speedtest;

#ifndef NATIVE_TEST
    Preferences _prefs;

    // BLE via microble
    class BleHandler : public MicroBLE::MessageHandler {
    public:
        explicit BleHandler(Relay& relay) : _relay(relay) {}
        void onMessage(uint8_t slot, const uint8_t* data, size_t len) override;
        void onConnect(uint8_t slot) override;
        void onDisconnect(uint8_t slot) override;
    private:
        Relay& _relay;
    };

    BleHandler _bleHandler{*this};
    MicroBLE::BleMessageService _bleService;
    NimBLECharacteristic* _statsChar = nullptr;
#endif

    // Presence tracking
    struct PresenceState {
        uint8_t senderId = 0;
        Presence last = Presence::Offline;
        bool tracked = false;
    };
    std::array<PresenceState, MAX_SENDERS> _presence = {};
};
