#pragma once

#include <stdint.h>
#include <stddef.h>
#include "BleFragmentation.h"

#ifndef NATIVE_TEST
#include "BleGattService.h"
#include <array>
#include <atomic>

namespace MicroBLE {

// ---------------------------------------------------------------------------
// MessageHandler — callback interface for complete messages
// ---------------------------------------------------------------------------
class MessageHandler {
public:
    virtual ~MessageHandler() = default;
    virtual void onMessage(uint8_t slot, const uint8_t* data, size_t len) = 0;
    virtual void onConnect(uint8_t slot) = 0;
    virtual void onDisconnect(uint8_t slot) = 0;
};

// ---------------------------------------------------------------------------
// BleMessageService — chunked messaging over BLE GATT
//
// Composes BleGattService + START/END fragmentation + cross-task RX queue.
//
// TX: sendMessage() auto-fragments into MTU-sized chunks.
// RX: incoming writes are reassembled, queued cross-task, and delivered
//     as complete messages via MessageHandler::onMessage() from loop().
//
// Template parameters:
//   MaxMessageSize — max reassembled message size per client
//   MaxClients     — max concurrent BLE connections
//   QueueSize      — RX signal ring buffer depth
// ---------------------------------------------------------------------------
template <size_t MaxMessageSize = 4096, uint8_t MaxClients = 3, uint8_t QueueSize = 4>
class BleMessageService : public GattHandler {
public:
    static constexpr uint8_t BACKPRESSURE_RETRIES = 10;

    void begin(MessageHandler* handler, const GattConfig& config) {
        _handler = handler;
        _gatt.begin(this, config);
    }

    // Call from main loop — drains RX queue, calls handler
    void loop() {
        processRxQueue();
    }

    // TX: auto-fragments and sends
    void sendMessage(uint8_t slot, const uint8_t* data, size_t len) {
        if (slot >= MaxClients || !_gatt.isConnected(slot)) return;
        uint16_t maxPayload = payloadSize(slot);

        bleFragmentSend(data, len, maxPayload, [&](const uint8_t* frag, size_t fragLen) {
            _gatt.send(slot, frag, fragLen);
        });
    }

    // Delegated accessors
    uint16_t mtu(uint8_t slot) const { return _gatt.mtu(slot); }
    bool isConnected(uint8_t slot) const { return _gatt.isConnected(slot); }
    uint8_t connectedCount() const { return _gatt.connectedCount(); }
    uint16_t connHandle(uint8_t slot) const { return _gatt.connHandle(slot); }
    BleGattService& gatt() { return _gatt; }

    // Max message payload for a client (MTU - 3 ATT overhead)
    uint16_t payloadSize(uint8_t slot) const {
        uint16_t m = _gatt.mtu(slot);
        return m >= 23 ? m - 3 : 20;
    }

    // --- GattHandler (called on NimBLE task) ---

    void onConnect(uint8_t slot) override {
        if (_handler) _handler->onConnect(slot);
    }

    void onDisconnect(uint8_t slot) override {
        if (slot < MaxClients) {
            _clients[slot].reassembler.reset();
            _clients[slot].messageReady.store(false, std::memory_order_relaxed);
        }
        if (_handler) _handler->onDisconnect(slot);
    }

    void onMTUChange(uint8_t slot, uint16_t mtu) override {
        (void)slot; (void)mtu;
    }

    void onWrite(uint8_t slot, const uint8_t* data, size_t len) override {
        if (slot >= MaxClients) return;
        auto& client = _clients[slot];

        // Backpressure: wait if main task hasn't consumed previous message
        if (client.messageReady.load(std::memory_order_acquire)) {
            for (uint8_t i = 0; i < BACKPRESSURE_RETRIES; i++) {
                vTaskDelay(1);
                if (!client.messageReady.load(std::memory_order_acquire)) break;
            }
            if (client.messageReady.load(std::memory_order_acquire)) return; // Drop
        }

        if (!client.reassembler.feed(data, len)) return; // Waiting for more fragments

        client.messageReady.store(true, std::memory_order_release);

        // Queue signal for main task
        for (uint8_t i = 0; i <= BACKPRESSURE_RETRIES; i++) {
            uint8_t head = _rxHead.load(std::memory_order_relaxed);
            uint8_t next = (head + 1) % QueueSize;
            if (next != _rxTail.load(std::memory_order_acquire)) {
                _rxQueue[head] = slot;
                _rxHead.store(next, std::memory_order_release);
                return;
            }
            if (i < BACKPRESSURE_RETRIES) vTaskDelay(1);
        }

        // Queue full — drop
        client.reassembler.reset();
        client.messageReady.store(false, std::memory_order_release);
    }

private:
    void processRxQueue() {
        uint8_t tail = _rxTail.load(std::memory_order_relaxed);
        while (tail != _rxHead.load(std::memory_order_acquire)) {
            uint8_t slot = _rxQueue[tail];
            auto& client = _clients[slot];

            if (client.messageReady.load(std::memory_order_acquire) && _handler) {
                _handler->onMessage(slot, client.reassembler.buffer(), client.reassembler.length());
                client.reassembler.reset();
                client.messageReady.store(false, std::memory_order_release);
            }

            tail = (tail + 1) % QueueSize;
            _rxTail.store(tail, std::memory_order_release);
        }
    }

    MessageHandler* _handler = nullptr;
    BleGattService _gatt;

    struct ClientState {
        BleReassembler<MaxMessageSize> reassembler;
        std::atomic<bool> messageReady{false};
    };
    std::array<ClientState, MaxClients> _clients = {};

    std::array<uint8_t, QueueSize> _rxQueue = {};
    std::atomic<uint8_t> _rxHead{0};
    std::atomic<uint8_t> _rxTail{0};
};

} // namespace MicroBLE

#endif // NATIVE_TEST
