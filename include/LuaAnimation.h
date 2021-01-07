#ifndef GARLAND_LUA_ANIMATION
#define GARLAND_LUA_ANIMATION

#include <Arduino.h>
#include <lua.hpp>
#include <LuaBridge/LuaBridge.h>

#include <FastLED.h>

#include "CallResult.h"
#include "GlobalAnimationEnv.h"
#include "Animation.h"
#include "LuaRefHolder.h"

class LuaAnimation
{
public:
    LuaAnimation(String& name);
    virtual ~LuaAnimation();
    CallResult<void*> begin(String& shader, GlobalAnimationEnv* globalAnimationEnv);
    CallResult<void*> apply(CRGB *leds, size_t size);

    String getName();
private:
    String name;

    lua_State* luaState;
    LuaRefHolder* luaRefHolder;
};

#endif //GARLAND_LUA_ANIMATION