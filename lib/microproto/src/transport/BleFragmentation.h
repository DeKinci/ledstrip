#ifndef MICROPROTO_TRANSPORT_BLE_FRAGMENTATION_H
#define MICROPROTO_TRANSPORT_BLE_FRAGMENTATION_H

#include <stdint.h>
#include <stddef.h>
#include <cstring>

namespace MicroProto {

/**
 * BLE message fragmentation/reassembly.
 *
 * BLE notifications are limited to MTU-3 bytes. This layer transparently
 * fragments large protocol messages into MTU-sized chunks and reassembles
 * them on the receiving end.
 *
 * Each BLE notification/write carries a 1-byte fragment header:
 *
 *   Bit 7 (0x80): START — first (or only) fragment
 *   Bit 6 (0x40): END   — last (or only) fragment
 *   Bits 0-5:     reserved (0)
 *
 *   0xC0 = complete message (no fragmentation needed)
 *   0x80 = first fragment of multi-part message
 *   0x00 = middle fragment
 *   0x40 = last fragment
 *
 * BLE guarantees in-order delivery per connection, so no sequence numbers needed.
 * Overhead: 1 byte per BLE notification/write.
 */

struct BleFragHeader {
    static constexpr uint8_t START = 0x80;
    static constexpr uint8_t END   = 0x40;
    static constexpr uint8_t COMPLETE = START | END;  // 0xC0
};

// Max BLE 5 ATT payload: MTU 517 - 3 ATT overhead = 514. Fragment = 1 header + payload.
static constexpr size_t BLE_MAX_FRAG_BUF = 515;

/**
 * Splits a message into fragments and calls sendFn for each.
 *
 * sendFn signature: void(const uint8_t* data, size_t length)
 *   — called once per fragment, each <= mtuPayload bytes
 *
 * mtuPayload = MTU - 3 (ATT overhead). The fragment header occupies 1 byte
 * of this, so each fragment carries up to (mtuPayload - 1) bytes of message data.
 *
 * Returns number of fragments sent.
 */
template <typename SendFn>
size_t bleFragmentSend(const uint8_t* data, size_t length, uint16_t mtuPayload, SendFn sendFn) {
    if (mtuPayload < 2) return 0;  // Need room for at least header + 1 byte

    const size_t chunkSize = mtuPayload - 1;  // 1 byte for fragment header
    size_t offset = 0;
    size_t fragCount = 0;

    // Fast path: message fits in one notification
    if (length <= chunkSize) {
        uint8_t frag[BLE_MAX_FRAG_BUF];
        frag[0] = BleFragHeader::COMPLETE;
        memcpy(frag + 1, data, length);
        sendFn(frag, length + 1);
        return 1;
    }

    // Multi-fragment send
    while (offset < length) {
        size_t remaining = length - offset;
        size_t thisChunk = remaining < chunkSize ? remaining : chunkSize;
        bool isFirst = (offset == 0);
        bool isLast = (offset + thisChunk >= length);

        uint8_t header = 0;
        if (isFirst) header |= BleFragHeader::START;
        if (isLast)  header |= BleFragHeader::END;

        uint8_t frag[BLE_MAX_FRAG_BUF];
        frag[0] = header;
        memcpy(frag + 1, data + offset, thisChunk);
        sendFn(frag, thisChunk + 1);

        offset += thisChunk;
        fragCount++;
    }

    return fragCount;
}

/**
 * Per-client reassembly buffer for incoming fragmented messages.
 *
 * Usage:
 *   1. Call feed() with each incoming BLE write (including fragment header)
 *   2. If feed() returns true, the complete message is in buffer()/length()
 *   3. Process the message, then call reset()
 *
 * Template parameter MaxSize is the maximum reassembled message size.
 */
template <size_t MaxSize>
class BleReassembler {
public:
    /**
     * Feed a raw BLE write (with fragment header byte).
     * Returns true when a complete message is ready.
     */
    bool feed(const uint8_t* data, size_t length) {
        if (length < 1) return false;

        uint8_t header = data[0];
        const uint8_t* payload = data + 1;
        size_t payloadLen = length - 1;

        bool isStart = (header & BleFragHeader::START) != 0;
        bool isEnd   = (header & BleFragHeader::END) != 0;

        if (isStart) {
            // New message — reset buffer
            _length = 0;
            _active = true;
        }

        if (!_active) {
            // Got a continuation/end without a start — discard
            return false;
        }

        // Append payload
        if (_length + payloadLen > MaxSize) {
            // Overflow — discard this message
            _active = false;
            _length = 0;
            return false;
        }

        memcpy(_buffer + _length, payload, payloadLen);
        _length += payloadLen;

        if (isEnd) {
            _active = false;
            return true;  // Complete message ready
        }

        return false;
    }

    const uint8_t* buffer() const { return _buffer; }
    size_t length() const { return _length; }

    void reset() {
        _length = 0;
        _active = false;
    }

private:
    uint8_t _buffer[MaxSize] = {};
    size_t _length = 0;
    bool _active = false;
};

} // namespace MicroProto

#endif // MICROPROTO_TRANSPORT_BLE_FRAGMENTATION_H
