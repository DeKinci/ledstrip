#include "LuaAnimation.h"

#include "Anime.h"
#include <Logger.h>

static const char* TAG = "Lua";
#ifdef MIC_ENABLED
#include "input/MicInput.hpp"
#endif

#ifdef ARDUINO
#include <multi_heap.h>
#endif

// ============================================================================
// Color packing: 0x00RRGGBB as lua_Integer (32-bit with LUA_32BITS)
// ============================================================================

static inline lua_Integer packColor(uint8_t r, uint8_t g, uint8_t b) {
    return (static_cast<lua_Integer>(r) << 16) |
           (static_cast<lua_Integer>(g) << 8) |
           static_cast<lua_Integer>(b);
}

static inline CRGB unpackColor(lua_Integer c) {
    return CRGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

static inline uint8_t clamp_byte(lua_Number v) {
    int i = static_cast<int>(v);
    return static_cast<uint8_t>((i % 256 + 256) % 256);
}

// ============================================================================
// Environment metatable (__index)
// ============================================================================

// Populate env table with current values (called each frame)
static void populateEnv(lua_State* L, int envRef) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, envRef);

    lua_pushnumber(L, Anime::getTime() / 1000.0);
    lua_setfield(L, -2, "time");

    lua_pushnumber(L, Anime::getDeltaTime());
    lua_setfield(L, -2, "dt");

    lua_pushinteger(L, Anime::getIter());
    lua_setfield(L, -2, "frame");

    lua_pushnumber(L, Anime::getBrightness() / 255.0);
    lua_setfield(L, -2, "lum");

    lua_pushinteger(L, Anime::getColor());
    lua_setfield(L, -2, "clr");

    lua_pushnumber(L, Anime::getSpeed());
    lua_setfield(L, -2, "spd");

#ifdef MIC_ENABLED
    lua_pushnumber(L, MicInput::getVolume());
    lua_setfield(L, -2, "volume");
#endif

    lua_pop(L, 1);
}

// ============================================================================
// Color constructors: rgb() and hsv()
// 3 args → return packed color
// 4 args → legacy write-to-buffer (backward compat)
// ============================================================================

// rgb(r, g, b) → packed color
static int lua_rgb(lua_State *L) {
    uint8_t r = clamp_byte(lua_tonumber(L, 1));
    uint8_t g = clamp_byte(lua_tonumber(L, 2));
    uint8_t b = clamp_byte(lua_tonumber(L, 3));
    lua_pushinteger(L, packColor(r, g, b));
    return 1;
}

// hsv(h, s, v) → packed color
static int lua_hsv(lua_State *L) {
    uint8_t h = clamp_byte(lua_tonumber(L, 1));
    uint8_t s = static_cast<uint8_t>(fmin(fmax(lua_tonumber(L, 2), 0.0), 1.0) * 255);
    uint8_t v = static_cast<uint8_t>(fmin(fmax(lua_tonumber(L, 3), 0.0), 1.0) * 255);
    CRGB c = CHSV(h, s, v);
    lua_pushinteger(L, packColor(c.r, c.g, c.b));
    return 1;
}

// blend(c1, c2, factor) → packed color
static int lua_blend(lua_State *L) {
    CRGB c1 = unpackColor(lua_tointeger(L, 1));
    CRGB c2 = unpackColor(lua_tointeger(L, 2));
    uint8_t amt = static_cast<uint8_t>(fmin(fmax(lua_tonumber(L, 3), 0.0), 1.0) * 255);
    CRGB result = blend(c1, c2, amt);
    lua_pushinteger(L, packColor(result.r, result.g, result.b));
    return 1;
}

// ============================================================================
// Segment userdata
// ============================================================================

static const char* SEGMENT_MT = "LedSegment";

struct LuaSegment {
    SegmentView* view;
};

static LuaSegment* checkSegment(lua_State *L, int idx = 1) {
    return static_cast<LuaSegment*>(luaL_checkudata(L, idx, SEGMENT_MT));
}

// Lua 1-based index → C 0-based, with optional reverse
static inline int resolveIdx(const SegmentView* view, int luaIdx) {
    int i = luaIdx - 1;  // 1-based → 0-based
    if (segReversed(*view->data)) i = view->data->ledCount - 1 - i;
    return i;
}

static int segment_index(lua_State *L) {
    LuaSegment* seg = checkSegment(L);

    if (lua_isinteger(L, 2)) {
        int i = static_cast<int>(lua_tointeger(L, 2));
        if (i >= 1 && i <= seg->view->data->ledCount) {
            const CRGB& c = seg->view->leds[resolveIdx(seg->view, i)];
            lua_pushinteger(L, packColor(c.r, c.g, c.b));
        } else {
            lua_pushinteger(L, 0);
        }
        return 1;
    }

    const char* key = lua_tostring(L, 2);
    if (!key) { lua_pushnil(L); return 1; }

    // Properties
    if (strcmp(key, "count") == 0)    { lua_pushinteger(L, seg->view->data->ledCount); return 1; }
    if (strcmp(key, "x") == 0)        { lua_pushinteger(L, seg->view->data->x); return 1; }
    if (strcmp(key, "y") == 0)        { lua_pushinteger(L, seg->view->data->y); return 1; }
    if (strcmp(key, "rotation") == 0) { lua_pushinteger(L, seg->view->data->rotation); return 1; }
    if (strcmp(key, "width") == 0)    { lua_pushinteger(L, seg->view->data->width); return 1; }
    if (strcmp(key, "height") == 0)   { lua_pushinteger(L, seg->view->data->height); return 1; }

    // Methods — look up in metatable
    luaL_getmetatable(L, SEGMENT_MT);
    lua_pushstring(L, key);
    lua_rawget(L, -2);
    return 1;
}

// seg[i] = color → write (1-based)
static int segment_newindex(lua_State *L) {
    LuaSegment* seg = checkSegment(L);
    int i = static_cast<int>(luaL_checkinteger(L, 2));
    lua_Integer color = luaL_checkinteger(L, 3);
    if (i >= 1 && i <= seg->view->data->ledCount) {
        seg->view->leds[resolveIdx(seg->view, i)] = unpackColor(color);
    }
    return 0;
}

// #seg → count
static int segment_len(lua_State *L) {
    LuaSegment* seg = checkSegment(L);
    lua_pushinteger(L, seg->view->data->ledCount);
    return 1;
}

// for i in led do ... end
// Generic for calls: i = led(state, control) each iteration
// led is the iterator function itself (via __call)
static int segment_call(lua_State *L) {
    LuaSegment* seg = checkSegment(L, 1);
    // Control variable is arg 3 (first call: nil, then previous i)
    int i = lua_isnil(L, 3) ? 0 : static_cast<int>(lua_tointeger(L, 3));
    i++;
    if (i > seg->view->data->ledCount) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, i);
    return 1;
}

// ============================================================================
// Segment transforms
// ============================================================================

// seg:fill(color)
static int segment_fill(lua_State *L) {
    LuaSegment* seg = checkSegment(L);
    CRGB c = unpackColor(luaL_checkinteger(L, 2));
    for (int i = 0; i < seg->view->data->ledCount; i++) {
        seg->view->leds[i] = c;
    }
    return 0;
}

// seg:fade(factor) — multiply all by factor (0..1)
static int segment_fade(lua_State *L) {
    LuaSegment* seg = checkSegment(L);
    uint8_t scale = static_cast<uint8_t>(fmin(fmax(luaL_checknumber(L, 2), 0.0), 1.0) * 255);
    for (int i = 0; i < seg->view->data->ledCount; i++) {
        seg->view->leds[i].nscale8(scale);
    }
    return 0;
}

// seg:blur(amount) — blur the segment (0..255)
static int segment_blur(lua_State *L) {
    LuaSegment* seg = checkSegment(L);
    uint8_t amount = static_cast<uint8_t>(luaL_checkinteger(L, 2));
    blur1d(seg->view->leds, seg->view->data->ledCount, amount);
    return 0;
}

// seg:shift(n) — rotate colors by n positions (positive = right)
static int segment_shift(lua_State *L) {
    LuaSegment* seg = checkSegment(L);
    int n = static_cast<int>(luaL_checkinteger(L, 2));
    int count = seg->view->data->ledCount;
    if (count <= 1) return 0;

    n = ((n % count) + count) % count;  // normalize to positive
    if (n == 0) return 0;

    // Reverse-based rotation (in-place, no allocation)
    auto reverse = [](CRGB* arr, int lo, int hi) {
        while (lo < hi) {
            CRGB tmp = arr[lo];
            arr[lo] = arr[hi];
            arr[hi] = tmp;
            lo++; hi--;
        }
    };
    reverse(seg->view->leds, 0, count - 1);
    reverse(seg->view->leds, 0, n - 1);
    reverse(seg->view->leds, n, count - 1);
    return 0;
}

// seg:mirror() — mirror first half onto second half
static int segment_mirror(lua_State *L) {
    LuaSegment* seg = checkSegment(L);
    int count = seg->view->data->ledCount;
    int half = count / 2;
    for (int i = 0; i < half; i++) {
        seg->view->leds[count - 1 - i] = seg->view->leds[i];
    }
    return 0;
}

// seg:set(col, row, color) — matrix addressing (1-based)
static int segment_set(lua_State *L) {
    LuaSegment* seg = checkSegment(L);
    int col = static_cast<int>(luaL_checkinteger(L, 2)) - 1;  // 1-based → 0-based
    int row = static_cast<int>(luaL_checkinteger(L, 3)) - 1;
    lua_Integer color = luaL_checkinteger(L, 4);

    int w = seg->view->data->width;
    int h = seg->view->data->height;
    if (w == 0 || h == 0 || col < 0 || col >= w || row < 0 || row >= h) return 0;

    int idx;
    if (segSerpentine(*seg->view->data) && (row & 1)) {
        idx = row * w + (w - 1 - col);
    } else {
        idx = row * w + col;
    }

    if (idx >= 0 && idx < seg->view->data->ledCount) {
        seg->view->leds[idx] = unpackColor(color);
    }
    return 0;
}

// ============================================================================
// Metatable registration
// ============================================================================

static void registerSegmentMetatable(lua_State *L) {
    luaL_newmetatable(L, SEGMENT_MT);

    lua_pushcfunction(L, segment_index);
    lua_setfield(L, -2, "__index");

    lua_pushcfunction(L, segment_newindex);
    lua_setfield(L, -2, "__newindex");

    lua_pushcfunction(L, segment_len);
    lua_setfield(L, -2, "__len");

    lua_pushcfunction(L, segment_call);
    lua_setfield(L, -2, "__call");

    // Methods (looked up by segment_index when key is string)
    lua_pushcfunction(L, segment_fill);
    lua_setfield(L, -2, "fill");

    lua_pushcfunction(L, segment_fade);
    lua_setfield(L, -2, "fade");

    lua_pushcfunction(L, segment_blur);
    lua_setfield(L, -2, "blur");

    lua_pushcfunction(L, segment_shift);
    lua_setfield(L, -2, "shift");

    lua_pushcfunction(L, segment_mirror);
    lua_setfield(L, -2, "mirror");

    lua_pushcfunction(L, segment_set);
    lua_setfield(L, -2, "set");

    lua_pop(L, 1);  // pop metatable
}

static void pushSegment(lua_State *L, SegmentView* view) {
    LuaSegment* ud = static_cast<LuaSegment*>(lua_newuserdata(L, sizeof(LuaSegment)));
    ud->view = view;
    luaL_getmetatable(L, SEGMENT_MT);
    lua_setmetatable(L, -2);
}

// ============================================================================
// LuaAnimation
// ============================================================================

// Pool allocator: all Lua memory comes from a fixed buffer.
// Falls back to regular malloc if pool is exhausted.
void* LuaAnimation::luaPoolAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    (void)osize;
    auto* self = static_cast<LuaAnimation*>(ud);

#ifdef ARDUINO
    auto heap = static_cast<multi_heap_handle_t>(self->luaHeap);
    if (nsize == 0) {
        multi_heap_free(heap, ptr);
        return nullptr;
    }
    if (ptr == nullptr) {
        return multi_heap_malloc(heap, nsize);
    }
    return multi_heap_realloc(heap, ptr, nsize);
#else
    // Native builds: standard allocator
    if (nsize == 0) { free(ptr); return nullptr; }
    return realloc(ptr, nsize);
#endif
}

LuaAnimation::LuaAnimation(String &name) {
    LuaAnimation::name = name;
    coRef = LUA_NOREF;
    envRef = LUA_NOREF;

#ifdef ARDUINO
    luaPool = new uint8_t[LUA_POOL_SIZE];
    luaHeap = multi_heap_register(luaPool, LUA_POOL_SIZE);
    luaState = lua_newstate(luaPoolAlloc, this);
#else
    luaPool = nullptr;
    luaHeap = nullptr;
    luaState = luaL_newstate();
#endif
}

LuaAnimation::~LuaAnimation() {
    lua_close(luaState);
#ifdef ARDUINO
    delete[] luaPool;
#endif
}

CallResult<void *> LuaAnimation::begin(String &shader) {
    luaL_openlibs(luaState);

    // Env table — populated each frame, passed as arg to draw()
    lua_newtable(luaState);
    lua_pushvalue(luaState, -1);        // dup for registry ref
    envRef = luaL_ref(luaState, LUA_REGISTRYINDEX);
    lua_setglobal(luaState, "e");

    // Color constructors (dual-mode: 3 args = return color, 4 args = legacy write)
    lua_pushcfunction(luaState, lua_rgb);
    lua_setglobal(luaState, "rgb");

    lua_pushcfunction(luaState, lua_hsv);
    lua_setglobal(luaState, "hsv");

    lua_pushcfunction(luaState, lua_blend);
    lua_setglobal(luaState, "blend");

    // Segment metatable
    registerSegmentMetatable(luaState);

    // Lua helpers
    // Lua helpers
    luaL_dostring(luaState,
        "_rs = 0\n"
        "function _hash(s, n)\n"
        "  local x = math.sin(s * 127.1 + n * 311.7) * 43758.5453\n"
        "  return x - math.floor(x)\n"
        "end\n"
        "function _smooth(a, b, period)\n"
        "  _rs = _rs + 1\n"
        "  local t = e.time / period\n"
        "  local slot = math.floor(t)\n"
        "  local frac = t - slot\n"
        "  frac = frac * frac * (3 - 2 * frac)\n"
        "  local ha = _hash(_rs, slot)\n"
        "  return a + (ha + (_hash(_rs, slot + 1) - ha) * frac) * (b - a)\n"
        "end\n"
        "function rand(a, b, period)\n"
        "  if not a then return math.random() end\n"
        "  if not b then return math.random(a) end\n"
        "  if period then return _smooth(a, b, period) end\n"
        "  if a % 1 == 0 and b % 1 == 0 then return math.random(a, b) end\n"
        "  return a + math.random() * (b - a)\n"
        "end\n"
        "function jitter(v, r, period)\n"
        "  if period then return v + _smooth(-r, r, period) end\n"
        "  return v + rand(-r, r)\n"
        "end\n"
        "function lerp(a, b, t) return a + (b - a) * t end\n"
    );

    // Coroutine helpers
    luaL_dostring(luaState,
        "function over(duration, fn)\n"
        "  local start = e.time\n"
        "  repeat\n"
        "    local t = math.min(1, (e.time - start) / duration)\n"
        "    fn(t)\n"
        "    coroutine.yield()\n"
        "  until t >= 1\n"
        "end\n"
        "function wait(duration)\n"
        "  over(duration, function() end)\n"
        "end\n"
        "function frame(duration)\n"
        "  coroutine.yield()\n"
        "  if duration then wait(duration) end\n"
        "end\n"
    );

    int loadShaderCode = luaL_dostring(luaState, shader.c_str());
    if (loadShaderCode) {
        const char *err = lua_tostring(luaState, -1);
        lua_settop(luaState, 0);
        LOG_ERROR(TAG, "Load error: %s", err);
        return CallResult<void *>(nullptr, 400, "Error loading code: %s", err);
    }

    coRef = LUA_NOREF;
    return CallResult<void *>(nullptr, 200);
}

CallResult<void *> LuaAnimation::apply(CRGB *leds, size_t size,
                                        const SegmentView* segments, uint8_t segmentCount,
                                        SegmentView* allLedsView) {
    // Push named segments as globals
    for (uint8_t i = 0; i < segmentCount; i++) {
        const char* name = segName(*segments[i].data);
        if (name && name[0]) {
            pushSegment(luaState, const_cast<SegmentView*>(&segments[i]));
            lua_setglobal(luaState, name);
        }
    }

    // Always push "led" covering all LEDs
    pushSegment(luaState, allLedsView);
    lua_setglobal(luaState, "led");

    // Update env table and reset per-frame counters
    populateEnv(luaState, envRef);
    lua_pushinteger(luaState, 0);
    lua_setglobal(luaState, "_rs");

    // Create or restart coroutine wrapping draw()
    lua_State* co = nullptr;

    if (coRef != LUA_NOREF) {
        lua_rawgeti(luaState, LUA_REGISTRYINDEX, coRef);
        co = lua_tothread(luaState, -1);
        lua_pop(luaState, 1);

        if (co && lua_status(co) == LUA_OK && lua_gettop(co) == 0) {
            luaL_unref(luaState, LUA_REGISTRYINDEX, coRef);
            coRef = LUA_NOREF;
            co = nullptr;
        }
    }

    if (!co) {
        lua_getglobal(luaState, "draw");
        if (!lua_isfunction(luaState, -1)) {
            lua_pop(luaState, 1);
            return CallResult<void *>(nullptr, 400, "Shader error: no draw() function defined");
        }
        lua_pop(luaState, 1);

        co = lua_newthread(luaState);
        coRef = luaL_ref(luaState, LUA_REGISTRYINDEX);
        lua_getglobal(co, "draw");
    }

    int nargs = lua_gettop(co) > 0 ? lua_gettop(co) - 1 : 0;
    int status = lua_resume(co, luaState, nargs);

    if (status == LUA_YIELD) {
        lua_settop(co, 0);
    } else if (status == LUA_OK) {
        lua_settop(co, 0);
    } else {
        const char *error = lua_tostring(co, -1);
        LOG_ERROR(TAG, "Runtime error: %s", error ? error : "unknown");
        lua_settop(co, 0);
        luaL_unref(luaState, LUA_REGISTRYINDEX, coRef);
        coRef = LUA_NOREF;
        lua_gc(luaState, LUA_GCCOLLECT, 0);
        return CallResult<void *>(nullptr, 500, "Shader error: %s", error ? error : "unknown");
    }

    return CallResult<void *>(nullptr, 200);
}

String LuaAnimation::getName() { return name; }
