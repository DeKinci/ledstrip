#ifndef GARLAND_LUA_ANIMATION
#define GARLAND_LUA_ANIMATION

#include <Arduino.h>
#include <lua.hpp>

#include <FastLED.h>

#include "core/CallResult.h"
#include "Animation.h"
#include "LedSegment.h"

class LuaAnimation
{
public:
    LuaAnimation(String& name);
    virtual ~LuaAnimation();
    CallResult<void*> begin(String& shader);
    CallResult<void*> apply(CRGB *leds, size_t size,
                            const SegmentView* segments, uint8_t segmentCount,
                            SegmentView* allLedsView);

    String getName();
private:
    String name;

    lua_State* luaState;
    int coRef;       // Lua registry ref for the draw() coroutine
    int envRef;      // Lua registry ref for the env table

    // Pool allocator — all Lua allocations come from this fixed buffer
    static constexpr size_t LUA_POOL_SIZE = 51200;  // 50KB
    uint8_t* luaPool;
    void* luaHeap;  // multi_heap_handle_t (void* to avoid ESP-IDF header in .h)

    static void* luaPoolAlloc(void* ud, void* ptr, size_t osize, size_t nsize);
};

#endif //GARLAND_LUA_ANIMATION
