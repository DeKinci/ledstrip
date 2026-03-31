#ifndef MICROPROTO_STREAM_PROPERTY_H
#define MICROPROTO_STREAM_PROPERTY_H

#include "PropertyBase.h"
#include "PropertyStorage.h"
#include "TypeTraits.h"
#include "wire/TypeCodec.h"
#include <array>

#ifndef MICROPROTO_STREAM_PENDING_SIZE
#define MICROPROTO_STREAM_PENDING_SIZE 8
#endif

namespace MicroProto {

/**
 * StreamProperty<T, N> - Append-only ring buffer property
 *
 * A stream property stores events/entries in a ring buffer and broadcasts
 * them to connected clients. Unlike regular properties (which represent
 * current state and send full values), stream properties send only NEW
 * entries since the last broadcast.
 *
 * Template parameters:
 *   T - Entry type (must be trivially copyable / wire-safe struct)
 *   N - History capacity (0 = online-only, no history replay on connect)
 *
 * Behavior:
 *   N > 0: Ring buffer of N entries. On client connect, full buffer is sent.
 *          After connect, only new entries are sent. Optional NVS persistence.
 *   N = 0: No history. Entries are buffered briefly for broadcast, then
 *          discarded. Nothing sent on client connect.
 *
 * Usage:
 *   StreamProperty<LogEntry, 10> errorLog("sys/errorLog",
 *       PropertyLevel::LOCAL, "Error log",
 *       UIHints().setWidget(Widget::Stream::LOG),
 *       PB::PERSISTENT, PB::READONLY);
 *
 *   StreamProperty<Sample<float>, 0> temperature("diag/temp",
 *       PropertyLevel::LOCAL, "Temperature",
 *       UIHints().setWidget(Widget::Stream::TIMESERIES).setUnit("°C"),
 *       PB::NOT_PERSISTENT, PB::READONLY);
 *
 *   errorLog.push(entry);  // Appends, marks dirty for broadcast
 */
template<typename T, size_t N>
class StreamProperty : public PropertyBase {
public:
    static_assert(is_microproto_struct_v<T> || is_microproto_basic_type_v<T>,
        "StreamProperty element must be a basic type or trivially copyable struct");

    static constexpr size_t HISTORY_CAPACITY = N;
    static constexpr size_t BUFFER_CAPACITY = N > 0 ? N : MICROPROTO_STREAM_PENDING_SIZE;

    StreamProperty(
        const char* name,
        PropertyLevel level,
        const char* description = nullptr,
        UIHints uiHints = UIHints(),
        bool persistent = false,
        bool readonly = false,
        bool hidden = false,
        bool ble_exposed = false,
        uint8_t group_id = 0
    ) : PropertyBase(name, level, persistent, readonly, hidden, ble_exposed, group_id, description, uiHints)
    {
        _buffer.fill(T{});
    }

    // =========== Append ===========

    /**
     * Push an entry into the stream.
     * Marks dirty for broadcast. For N > 0, entry is stored in ring buffer.
     * For N = 0, entry is buffered until next broadcast then discarded.
     */
    void push(const T& entry) {
        _buffer[_head] = entry;
        _head = (_head + 1) % BUFFER_CAPACITY;
        if (_count < BUFFER_CAPACITY) _count++;
        if (_pendingCount < BUFFER_CAPACITY) _pendingCount++;
        notifyChange();
    }

    // =========== Read access ===========

    /** Number of entries currently in the buffer */
    size_t count() const { return _count; }

    /** Whether the buffer is empty */
    bool empty() const { return _count == 0; }

    /** Whether there are entries awaiting broadcast */
    bool hasPending() const { return _pendingCount > 0; }

    /** Get entry by index (0 = oldest in buffer) */
    const T& operator[](size_t index) const {
        size_t start = (_head + BUFFER_CAPACITY - _count) % BUFFER_CAPACITY;
        return _buffer[(start + index) % BUFFER_CAPACITY];
    }

    // =========== PropertyBase interface ===========

    uint8_t getTypeId() const override { return TYPE_STREAM; }

    size_t getSize() const override {
        return _count * sizeof(T);
    }

    const void* getData() const override {
        return _buffer.data();
    }

    void setData(const void* data, size_t size) override {
        // Streams are append-only, reject external writes
    }

    bool isContainer() const override { return true; }
    uint8_t getElementTypeId() const override { return TypeTraits<T>::type_id; }
    size_t getElementSize() const override { return sizeof(T); }
    size_t getElementCount() const override { return _count; }
    size_t getMaxElementCount() const override { return BUFFER_CAPACITY; }

    // =========== Stream interface ===========

    bool isStream() const override { return true; }

    /**
     * Encode full buffer contents (for initial sync on client connect).
     * For N = 0, encodes nothing (no history to replay).
     */
    bool encodeFullBuffer(WriteBuffer& buf) const override {
        if constexpr (N == 0) {
            // Online-only: no history to replay
            return buf.writeVarint(0) > 0;
        } else {
            return encodeEntries(buf, _count);
        }
    }

    /**
     * Encode only entries pushed since last clearPending().
     * Used for periodic broadcasts to already-connected clients.
     */
    bool encodePendingEntries(WriteBuffer& buf) const override {
        return encodeEntries(buf, _pendingCount);
    }

    /**
     * Mark all pending entries as sent. Called after broadcast.
     * For N = 0, also clears the buffer entirely.
     */
    void clearPending() override {
        _pendingCount = 0;
        if constexpr (N == 0) {
            _count = 0;
            _head = 0;
        }
    }

    // =========== Schema encoding ===========

    bool encodeTypeDefinition(WriteBuffer& buf) const override {
        // STREAM type marker
        if (!buf.writeByte(TYPE_STREAM)) return false;

        // History capacity hint (so clients know buffer size)
        if (buf.writeVarint(static_cast<uint32_t>(N)) == 0) return false;

        // Element type definition (recursive, same as LIST elements)
        return SchemaTypeEncoder::encodeElement<T>(buf, nullptr);
    }

    // =========== NVS persistence ===========

    bool saveToNVS() override {
        if constexpr (N == 0) {
            return true; // Nothing to persist for online-only streams
        } else {
#ifdef ARDUINO
            // Serialize: head(2) + count(2) + entries
            constexpr size_t headerSize = 4;
            constexpr size_t dataSize = headerSize + BUFFER_CAPACITY * sizeof(T);
            uint8_t blob[dataSize];

            // Header: head and count as uint16_t
            uint16_t h = static_cast<uint16_t>(_head);
            uint16_t c = static_cast<uint16_t>(_count);
            memcpy(blob, &h, 2);
            memcpy(blob + 2, &c, 2);

            // Entries
            memcpy(blob + headerSize, _buffer.data(), BUFFER_CAPACITY * sizeof(T));

            return PropertyStorage::saveRaw(name, blob, dataSize);
#else
            return true;
#endif
        }
    }

    bool loadFromNVS() override {
        if constexpr (N == 0) {
            return false;
        } else {
#ifdef ARDUINO
            constexpr size_t headerSize = 4;
            constexpr size_t dataSize = headerSize + BUFFER_CAPACITY * sizeof(T);
            uint8_t blob[dataSize];

            size_t loaded = PropertyStorage::loadRaw(name, blob, dataSize);
            if (loaded != dataSize) return false;

            uint16_t h, c;
            memcpy(&h, blob, 2);
            memcpy(&c, blob + 2, 2);

            if (h >= BUFFER_CAPACITY || c > BUFFER_CAPACITY) return false;

            _head = h;
            _count = c;
            _pendingCount = 0; // Don't re-broadcast on load

            memcpy(_buffer.data(), blob + headerSize, BUFFER_CAPACITY * sizeof(T));
            return true;
#else
            return false;
#endif
        }
    }

private:
    std::array<T, BUFFER_CAPACITY> _buffer;
    size_t _head = 0;           // Next write position
    size_t _count = 0;          // Total entries in buffer
    size_t _pendingCount = 0;   // Entries not yet broadcast

    /**
     * Encode the most recent `n` entries from the ring buffer.
     */
    bool encodeEntries(WriteBuffer& buf, size_t n) const {
        if (n > _count) n = _count;

        if (buf.writeVarint(static_cast<uint32_t>(n)) == 0) return false;

        // Start from the (n)th-most-recent entry
        size_t start = (_head + BUFFER_CAPACITY - n) % BUFFER_CAPACITY;
        for (size_t i = 0; i < n; i++) {
            size_t idx = (start + i) % BUFFER_CAPACITY;
            if (!TypeCodec::encode(buf, _buffer[idx])) return false;
        }
        return buf.ok();
    }
};

} // namespace MicroProto

#endif // MICROPROTO_STREAM_PROPERTY_H
