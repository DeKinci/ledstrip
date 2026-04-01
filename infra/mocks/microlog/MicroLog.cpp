// Native stub for MetricBase constructor.
// On Arduino, this is defined in MicroLog.cpp and auto-registers with Logger.
// On native, we just store name/unit — no Logger to register with.

#include <Gauge.h>

namespace MicroLog {

MetricBase::MetricBase(const char* name, const char* unit)
    : _name(name), _unit(unit) {}

} // namespace MicroLog
