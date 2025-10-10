#include "LuaAnimation.h"

#include "Anime.h"

LuaAnimation::LuaAnimation(String &name) {
    LuaAnimation::name = name;
    luaState = luaL_newstate();
}

LuaAnimation::~LuaAnimation() { lua_close(luaState); }

int env_index(lua_State *L) {
    const char *key = lua_tostring(L, 2);
    if (!key) {
        lua_pushnil(L);
        return 1;
    }

    if (strcmp(key, "millis") == 0) {
        lua_pushinteger(L, Anime::getTime());
    } else if (strcmp(key, "iteration") == 0) {
        lua_pushinteger(L, Anime::getIter());
    } else if (strcmp(key, "deltatime") == 0) {
        lua_pushnumber(L, Anime::getDeltaTime());
    } else if (strcmp(key, "brightness") == 0) {
        lua_pushnumber(L, Anime::getBrightness() / 255.0);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static CRGB *leds_buffer = nullptr;

static inline uint8_t clamp_byte(lua_Number v) {
    int i = static_cast<int>(v);
    return static_cast<uint8_t>((i % 256 + 256) % 256);
}

int lua_set_rgb(lua_State *L) {
    int index = static_cast<int>(lua_tointeger(L, 1));
    uint8_t r = clamp_byte(lua_tonumber(L, 2));
    uint8_t g = clamp_byte(lua_tonumber(L, 3));
    uint8_t b = clamp_byte(lua_tonumber(L, 4));
    if (leds_buffer && index >= 0) {
        leds_buffer[index] = CRGB(r, g, b);
    }
    return 0;
}

int lua_set_hsv(lua_State *L) {
    int index = static_cast<int>(lua_tointeger(L, 1));
    uint8_t h = clamp_byte(lua_tonumber(L, 2));
    uint8_t s = clamp_byte(lua_tonumber(L, 3));
    uint8_t v = clamp_byte(lua_tonumber(L, 4));
    if (leds_buffer && index >= 0) {
        leds_buffer[index] = CHSV(h, s, v);
    }
    return 0;
}

CallResult<void *> LuaAnimation::begin(String &shader) {
    luaL_openlibs(luaState);

    lua_newtable(luaState);  // env table

    lua_newtable(luaState);                  // metatable
    lua_pushcfunction(luaState, env_index);  // live access setup
    lua_setfield(luaState, -2, "__index");
    lua_setmetatable(luaState, -2);

    lua_setglobal(luaState, "env");

    // Bind set_led (leds are assigned later in apply)
    lua_pushcfunction(luaState, lua_set_rgb);
    lua_setglobal(luaState, "rgb");

    lua_pushcfunction(luaState, lua_set_hsv);
    lua_setglobal(luaState, "hsv");

    int loadShaderCode = luaL_dostring(luaState, shader.c_str());
    if (loadShaderCode) {
        const char *err = lua_tostring(luaState, -1);
        lua_settop(luaState, 0);
        return CallResult<void *>(nullptr, 400, "Error loading code: %s", err);
    }

    return CallResult<void *>(nullptr, 200);
}

CallResult<void *> LuaAnimation::apply(CRGB *leds, size_t size) {
    leds_buffer = leds;

    lua_getglobal(luaState, "draw");
    if (!lua_isfunction(luaState, -1)) {
        lua_pop(luaState, 1);
        return CallResult<void *>(nullptr, 400, "Shader error: no draw() function defined");
    }

    lua_pushinteger(luaState, size);  // pass led_count as argument

    if (lua_pcall(luaState, 1, 0, 0) != 0) {
        const char *error = lua_tostring(luaState, -1);
        lua_settop(luaState, 0);
        lua_gc(luaState, LUA_GCCOLLECT, 0);
        return CallResult<void *>(nullptr, 500, "Shader error: %s", error);
    }

    lua_settop(luaState, 0);
    leds_buffer = nullptr;
    return CallResult<void *>(nullptr, 200);
}

String LuaAnimation::getName() { return name; }
