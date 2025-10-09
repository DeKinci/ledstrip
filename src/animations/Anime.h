#ifndef GARLAND_ANIMATION_MANAGER
#define GARLAND_ANIMATION_MANAGER

#include <Arduino.h>
#include <vector>
#include <array>

#include "core/ShaderStorage.h"
#include "LuaAnimation.h"

#define CACHE_SIZE 1

namespace Anime
{
    CallResult<void *> connect();

    CallResult<void *> draw();
    void scheduleReload();
    CallResult<void *> select(String &shaderName);
    String getCurrent();
    size_t getCurrentLeds();
    void setCurrentLeds(size_t currentLeds);

    uint8_t getBrightness();
    void setBrightness(uint8_t brightness);

    void nextAnimation();
    void previousAnimation();

    uint32_t getTime();
    uint32_t getIter();
    float getDeltaTime();

    void sampleTime();
    void incIter();
};

#endif // GARLAND_ANIMATION_MANAGER