#include "FunctionBase.h"
#include <assert.h>

namespace MicroProto {

std::array<FunctionBase*, FunctionBase::MAX_FUNCTIONS> FunctionBase::byId = {};
uint8_t FunctionBase::count = 0;
uint8_t FunctionBase::nextId = 0;

FunctionBase::FunctionBase(
    const char* name,
    const ParamDef* params,
    uint8_t paramCount,
    uint8_t returnTypeId,
    const char* description,
    bool ble_exposed
) : id(nextId++),
    name(name),
    description(description),
    ble_exposed(ble_exposed),
    params(params),
    paramCount(paramCount),
    returnTypeId(returnTypeId)
{
    assert(id < MAX_FUNCTIONS && "Too many RPC functions — increase MICROPROTO_MAX_FUNCTIONS");
    byId[id] = this;
    count++;
}

} // namespace MicroProto
