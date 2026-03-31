#pragma once

#include <cstdint>
#include <cstdio>
#include <type_traits>

namespace MicroLog {

// Type-erased metric base — used by Logger to print all metrics in one line
class MetricBase {
public:
    MetricBase(const char* name, const char* unit);
    virtual ~MetricBase() = default;
    virtual int printValue(char* buf, size_t len) const = 0;
    const char* name() const { return _name; }
    const char* unit() const { return _unit; }
protected:
    const char* _name;
    const char* _unit;
};

// Callback for when a metric value is pushed (used by microlog-up)
template<typename T>
using MetricCallback = void(*)(uint32_t timestamp, T value);

template<typename T>
class Gauge : public MetricBase {
public:
    Gauge(const char* name, const char* unit = nullptr, uint32_t debounceMs = 1000)
        : MetricBase(name, unit), _debounceMs(debounceMs) {}

    void set(T value) {
#ifdef ARDUINO
        uint32_t now = millis();
#else
        uint32_t now = 0;
#endif
        bool changed = !_hasValue || (_lastValue != value);
        bool elapsed = (now - _lastPushTime) >= _debounceMs;
        if (changed || elapsed) {
            _lastValue = value;
            _lastPushTime = now;
            _hasValue = true;
            if (_callback) _callback(now, value);
        }
    }

    T get() const { return _lastValue; }
    void onPush(MetricCallback<T> cb) { _callback = cb; }

    int printValue(char* buf, size_t len) const override {
        if (!_hasValue) return snprintf(buf, len, "%s=-", _name);
        if constexpr (std::is_floating_point_v<T>)
            return _unit ? snprintf(buf, len, "%s=%.1f%s", _name, (double)_lastValue, _unit)
                         : snprintf(buf, len, "%s=%.1f", _name, (double)_lastValue);
        else if constexpr (std::is_signed_v<T>)
            return _unit ? snprintf(buf, len, "%s=%ld%s", _name, (long)_lastValue, _unit)
                         : snprintf(buf, len, "%s=%ld", _name, (long)_lastValue);
        else
            return _unit ? snprintf(buf, len, "%s=%lu%s", _name, (unsigned long)_lastValue, _unit)
                         : snprintf(buf, len, "%s=%lu", _name, (unsigned long)_lastValue);
    }

private:
    uint32_t _debounceMs;
    uint32_t _lastPushTime = 0;
    T _lastValue = {};
    bool _hasValue = false;
    MetricCallback<T> _callback = nullptr;
};

template<typename T>
class Counter : public MetricBase {
public:
    Counter(const char* name, const char* unit = nullptr)
        : MetricBase(name, unit) {}

    void increment(T delta = 1) { _total += delta; }
    T get() const { return _total; }
    void onPush(MetricCallback<T> cb) { _callback = cb; }

    int printValue(char* buf, size_t len) const override {
        if constexpr (std::is_signed_v<T>)
            return _unit ? snprintf(buf, len, "%s=%ld%s", _name, (long)_total, _unit)
                         : snprintf(buf, len, "%s=%ld", _name, (long)_total);
        else
            return _unit ? snprintf(buf, len, "%s=%lu%s", _name, (unsigned long)_total, _unit)
                         : snprintf(buf, len, "%s=%lu", _name, (unsigned long)_total);
    }

private:
    T _total = {};
    MetricCallback<T> _callback = nullptr;
};

} // namespace MicroLog
