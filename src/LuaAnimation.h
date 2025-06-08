#ifndef GARLAND_LUA_ANIMATION
#define GARLAND_LUA_ANIMATION

#include <Arduino.h>
#include <lua.hpp>

#include <FastLED.h>

#include "CallResult.h"
#include "Animation.h"

class LuaAnimation
{
public:
    LuaAnimation(String& name);
    virtual ~LuaAnimation();
    CallResult<void*> begin(String& shader);
    CallResult<void*> apply(CRGB *leds, size_t size);

    String getName();
private:
    String name;

    lua_State* luaState;
};

#endif //GARLAND_LUA_ANIMATION