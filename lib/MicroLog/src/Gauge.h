#ifndef MICROLOG_GAUGE_H
#define MICROLOG_GAUGE_H

#include <StreamProperty.h>
#include <array>

namespace MicroLog {

/**
 * Sample<T> - Timestamped value for timeseries streams.
 */
template<typename T>
struct Sample {
    uint32_t timestamp;
    T value;
};

/**
 * Gauge<T> - A timeseries variable representing a current value that changes over time.
 *
 * Wraps a StreamProperty<Sample<T>, 0> with built-in debounce.
 * Only pushes a new sample when the value changes or the debounce interval elapses.
 * Client/gateway accumulates history for graphing.
 *
 * Usage:
 *   MicroLog::Gauge<float> freeHeap("diag/freeHeap", "KB");
 *
 *   void loop() {
 *       freeHeap.set(ESP.getFreeHeap() / 1024.0f);
 *   }
 */
template<typename T>
class Gauge {
public:
    using PB = MicroProto::PropertyBase;

    Gauge(
        const char* name,
        const char* unit = nullptr,
        uint32_t debounceMs = 1000,
        MicroProto::UIColor color = MicroProto::UIColor::CYAN
    ) : _stream(
            name,
            MicroProto::PropertyLevel::LOCAL,
            name, // description = name for gauges
            MicroProto::UIHints()
                .setColor(color)
                .setWidget(MicroProto::Widget::Stream::TIMESERIES)
                .setUnit(unit),
            PB::NOT_PERSISTENT, PB::READONLY
        ),
        _debounceMs(debounceMs)
    {
    }

    /**
     * Set the current gauge value.
     * Pushes a sample if value changed or debounce interval elapsed.
     */
    void set(T value) {
#ifdef ARDUINO
        uint32_t now = millis();
#else
        uint32_t now = 0;
#endif

        bool changed = !_hasValue || (_lastValue != value);
        bool elapsed = (now - _lastPushTime) >= _debounceMs;

        if (changed || elapsed) {
            Sample<T> sample;
            sample.timestamp = now;
            sample.value = value;
            _stream.push(sample);
            _lastValue = value;
            _lastPushTime = now;
            _hasValue = true;
        }
    }

    /** Get the last set value */
    T get() const { return _lastValue; }

    /** Access the underlying stream property */
    auto& stream() { return _stream; }

private:
    MicroProto::StreamProperty<Sample<T>, 0> _stream;
    uint32_t _debounceMs;
    uint32_t _lastPushTime = 0;
    T _lastValue = {};
    bool _hasValue = false;
};

} // namespace MicroLog

#endif // MICROLOG_GAUGE_H
