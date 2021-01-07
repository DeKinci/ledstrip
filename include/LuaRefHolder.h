#ifndef LUA_REF_HOLDER
#define LUA_REF_HOLDER

#include <LuaBridge/LuaBridge.h>

class LuaRefHolder
{
public:
    luabridge::LuaRef ref;
    LuaRefHolder(luabridge::LuaRef &luaRef) : ref(luaRef) {};
};

#endif //LUA_REF_HOLDER