#include "LuaAnimation.h"

LuaAnimation::LuaAnimation(String& name) {
    LuaAnimation::name = name;
    luaState = luaL_newstate();
}

LuaAnimation::~LuaAnimation() {
    lua_close(luaState);
}

CallResult<void*> LuaAnimation::begin(String& shader, GlobalAnimationEnv* globalAnimationEnv) {
    int loadShaderCode = luaL_dostring(luaState, shader.c_str());
    if (loadShaderCode) {
        return CallResult<void*>(nullptr, 400, "Error loading code, lua message %d", loadShaderCode);
    }

    luaL_openlibs(luaState);
    // int compileShaderCode = lua_pcall(luaState, 0, 0, 0);
    // if (compileShaderCode) {
    //     // return CallResult<void*>(nullptr, 400, "Error running code, lua message %d", compileShaderCode);
    // }

    luabridge::getGlobalNamespace(luaState)
        .beginNamespace("env")
        .addProperty("millis", &(globalAnimationEnv->timeMillis), false)
        .addProperty("iteration", &(globalAnimationEnv->iteration), false)
        .endNamespace();
    return CallResult<void*>(nullptr, 200);
}

CallResult<void*> LuaAnimation::apply(CRGB *leds, size_t size) {
    luabridge::LuaRef colorFunc = luabridge::LuaRef(luabridge::getGlobal(luaState, "color"));
    if (colorFunc.isNil() || !colorFunc.isFunction()) {
        return CallResult<void*>(nullptr, 400, "No shader function \"color(int) -> (int, int, int)\" is present in the code");
    }

    for (int i = 0; i < size; i++) {
        luabridge::LuaRef ledColor = colorFunc(i);
        if (ledColor.isNil()) {
            // pixel is discarded, ignore
        } 
        else {
            if (!ledColor.isTable()) {
                return CallResult<void*>(nullptr, 400, "Shader function \"color(int)\" does not return a table");
            }
            if (!ledColor[1].isNumber() || !ledColor[2].isNumber() || !ledColor[3].isNumber()) {
                return CallResult<void*>(nullptr, 400, "Shader function \"color(int)\" returned table of not numbers");
            }
            leds[i] = CHSV(ledColor[1].cast<int>(), ledColor[2].cast<int>(), ledColor[3].cast<int>());
        }
    }
    return CallResult<void*>(nullptr, 200);
}

String LuaAnimation::getName() {
    return name;
}
