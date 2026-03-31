#ifndef MICROLOG_COUNTER_H
#define MICROLOG_COUNTER_H

#include "Gauge.h"

namespace MicroLog {

/**
 * Counter<T> - A monotonically increasing timeseries variable.
 *
 * Accumulates increments internally and pushes the current total
 * at the debounce rate.
 *
 * Usage:
 *   MicroLog::Counter<uint32_t> httpReqs("diag/httpReqs");
 *
 *   void handleRequest() {
 *       httpReqs.increment();
 *   }
 */
template<typename T>
class Counter {
public:
    using PB = MicroProto::PropertyBase;

    Counter(
        const char* name,
        const char* unit = nullptr,
        uint32_t debounceMs = 1000,
        MicroProto::UIColor color = MicroProto::UIColor::EMERALD
    ) : _stream(
            name,
            MicroProto::PropertyLevel::LOCAL,
            name,
            MicroProto::UIHints()
                .setColor(color)
                .setWidget(MicroProto::Widget::Stream::TIMESERIES)
                .setUnit(unit),
            PB::NOT_PERSISTENT, PB::READONLY
        ),
        _debounceMs(debounceMs)
    {
    }

    /** Increment the counter by delta (default 1) */
    void increment(T delta = 1) {
        _total += delta;
        _dirty = true;
        maybePush();
    }

    /** Get the current total */
    T get() const { return _total; }

    /**
     * Call from loop() to ensure samples are pushed at the debounce rate
     * even when increment() isn't called frequently.
     */
    void loop() {
        if (_dirty) maybePush();
    }

    /** Access the underlying stream property */
    auto& stream() { return _stream; }

private:
    MicroProto::StreamProperty<Sample<T>, 0> _stream;
    uint32_t _debounceMs;
    uint32_t _lastPushTime = 0;
    T _total = {};
    bool _dirty = false;

    void maybePush() {
#ifdef ARDUINO
        uint32_t now = millis();
#else
        uint32_t now = 0;
#endif

        if ((now - _lastPushTime) >= _debounceMs) {
            Sample<T> sample;
            sample.timestamp = now;
            sample.value = _total;
            _stream.push(sample);
            _lastPushTime = now;
            _dirty = false;
        }
    }
};

} // namespace MicroLog

#endif // MICROLOG_COUNTER_H
