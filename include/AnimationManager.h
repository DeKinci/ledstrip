#ifndef GARLAND_ANIMATION_MANAGER
#define GARLAND_ANIMATION_MANAGER

#include <Arduino.h>
#include <vector>

#include "ShaderStorage.h"
#include "GlobalAnimationEnv.h"
#include "LuaAnimation.h"
#include "SelectAnimationListener.h"

#define CACHE_SIZE 5

class AnimationManager
{
private:
    CRGB *leds;
    size_t size;
    int speed = 100;

    GlobalAnimationEnv* globalAnimationEnv;
    ShaderStorage *shaderStorage;

    std::vector<String>* shaders = new std::vector<String>();
    std::vector<LuaAnimation*>* loadedAnimations;

    uint16_t currentAnimationShaderIndex = 0;
    LuaAnimation* currentAnimation;
    long lastUpdate = 0;

    bool toReload = false;

    SelectAnimationListener* listener;
    void setCurrentAnimation(LuaAnimation* animation);

    CallResult<LuaAnimation*> loadCached(String& shaderName);
    CallResult<void*> reload();
public:
    AnimationManager(ShaderStorage *storage, GlobalAnimationEnv* globalAnimationEnv);

    template <uint8_t DATA_PIN>
    CallResult<void*> connect(int size) {
        CallResult<void*> loadResult = reload();
        if (loadResult.hasError()) {
            return loadResult;
        }

        this->size = size;
        this->leds = new CRGB[size];
        FastLED.addLeds<WS2812B, DATA_PIN, RGB>(leds, size).setCorrection(TypicalSMD5050);
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

    virtual ~AnimationManager();
};

#endif //GARLAND_ANIMATION_MANAGER