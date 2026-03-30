#ifndef MICROPROTO_FUNCTION_BASE_H
#define MICROPROTO_FUNCTION_BASE_H

#include <stdint.h>
#include <stddef.h>
#include <array>
#include <MicroFunction.h>
#include "wire/Buffer.h"

namespace MicroProto {

using microcore::MicroFunction;

// 0 = void (no return value)
constexpr uint8_t TYPE_VOID = 0x00;

/**
 * Parameter definition for RPC functions.
 */
struct ParamDef {
    const char* name;
    uint8_t typeId;  // TYPE_BOOL, TYPE_UINT8, etc.
};

/**
 * Base class for RPC functions. Mirrors PropertyBase's static registry pattern.
 *
 * Usage:
 *   static const ParamDef params[] = {{"slot", TYPE_UINT8}};
 *   FunctionBase savePreset("savePreset", params, 1, TYPE_BOOL, "Save to slot");
 *
 *   savePreset.onCall([](ReadBuffer& p, WriteBuffer& r) -> bool {
 *       uint8_t slot = p.readByte();
 *       bool ok = doSave(slot);
 *       r.writeByte(ok ? 1 : 0);
 *       return true;
 *   });
 */
class FunctionBase {
public:
    const uint8_t id;
    const char* name;
    const char* description;
    const bool ble_exposed;
    const ParamDef* params;
    const uint8_t paramCount;
    const uint8_t returnTypeId;  // TYPE_VOID = no return value

    // Static registry
#ifndef MICROPROTO_MAX_FUNCTIONS
#define MICROPROTO_MAX_FUNCTIONS 32
#endif
    static constexpr size_t MAX_FUNCTIONS = MICROPROTO_MAX_FUNCTIONS;
    static_assert(MICROPROTO_MAX_FUNCTIONS <= 255, "MAX_FUNCTIONS must fit in uint8_t");
    static std::array<FunctionBase*, MAX_FUNCTIONS> byId;
    static uint8_t count;

    static FunctionBase* find(uint8_t id) {
        if (id >= MAX_FUNCTIONS) return nullptr;
        return byId[id];
    }

#ifdef NATIVE_TEST
    static void resetRegistry() { byId = {}; count = 0; nextId = 0; }
#endif

    // Handler: reads params, writes result. Returns true for success.
    using Handler = MicroFunction<bool(ReadBuffer&, WriteBuffer&), 8>;

    FunctionBase(
        const char* name,
        const ParamDef* params = nullptr,
        uint8_t paramCount = 0,
        uint8_t returnTypeId = TYPE_VOID,
        const char* description = nullptr,
        bool ble_exposed = true
    );

    void onCall(Handler handler) { _handler = handler; }

    bool hasHandler() const { return static_cast<bool>(_handler); }

    /**
     * Invoke the handler with params, write result.
     * Returns true on success, false if no handler or handler fails.
     */
    bool invoke(ReadBuffer& params, WriteBuffer& result) {
        if (!_handler) return false;
        return _handler(params, result);
    }

private:
    static uint8_t nextId;
    Handler _handler;
};

} // namespace MicroProto

#endif // MICROPROTO_FUNCTION_BASE_H
