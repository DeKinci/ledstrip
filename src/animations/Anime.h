#ifndef GARLAND_ANIMATION_MANAGER
#define GARLAND_ANIMATION_MANAGER

#include <Arduino.h>
#include <vector>
#include <array>

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
    size_t getShaderCount();

    uint8_t getBrightness();
    void setBrightness(uint8_t brightness);

    void nextAnimation();
    void previousAnimation();

    uint32_t getTime();
    uint32_t getIter();
    float getDeltaTime();

    void sampleTime();
    void incIter();

    void wakeUp();  // Wake from power save mode

    void enableAtmosphericFade();   // Enable gradual brightness reduction (kerosene lamp effect)
    void disableAtmosphericFade();  // Disable atmospheric fade
    bool isAtmosphericFadeEnabled(); // Check if atmospheric fade is active
};

#endif // GARLAND_ANIMATION_MANAGER