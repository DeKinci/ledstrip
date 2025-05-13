#ifndef GARLAND_ANIMATION_MANAGER
#define GARLAND_ANIMATION_MANAGER

#include <Arduino.h>
#include <vector>

#include "ShaderStorage.h"
#include "GlobalAnimationEnv.h"
#include "LuaAnimation.h"
#include "SelectAnimationListener.h"

#define CACHE_SIZE 3
#define LED_MODEL WS2812

class AnimationManager
{
private:
    CRGB *leds = nullptr;
    size_t ledLimit = 0;
    size_t currentLeds = 0;
    int speed = 100;

    GlobalAnimationEnv* globalAnimationEnv;
    ShaderStorage *shaderStorage;

    std::vector<String>* shaders = new std::vector<String>();
    std::vector<LuaAnimation*>* loadedAnimations = new std::vector<LuaAnimation*>();

    uint16_t currentAnimationShaderIndex = 0;
    LuaAnimation* currentAnimation;
    long lastUpdate = 0;

    bool toReload = false;

    SelectAnimationListener* listener;
    void setCurrentAnimation(LuaAnimation* animation);

    CallResult<LuaAnimation*> loadCached(String& shaderName);
    CallResult<void*> reload();

public:
    AnimationManager(ShaderStorage *storage, GlobalAnimationEnv* globalAnimationEnv, size_t ledLimit);

    template <uint8_t DATA_PIN>
    CallResult<void*> connect() {
        CallResult<void*> loadResult = reload();
        if (loadResult.hasError()) {
            return loadResult;
        }
        
        FastLED.addLeds<LED_MODEL, DATA_PIN, RGB>(leds, ledLimit).setCorrection(TypicalSMD5050);
        FastLED.setBrightness(255);
        FastLED.clear(true);
        return CallResult<void*>(nullptr);
    }

    void previous();
    void next();
    void faster();
    void slower();
    CallResult<void*> draw();
    void scheduleReload();
    CallResult<void*> select(String& shaderName);
    String getCurrent();
    void setListener(SelectAnimationListener* listener);
    size_t getCurrentLeds() const;
    void setCurrentLeds(size_t currentLeds);

    virtual ~AnimationManager();
};

#endif //GARLAND_ANIMATION_MANAGER