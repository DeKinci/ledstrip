#include "Anime.h"

#include <Arduino.h>

#include "SocketController.h"

namespace {

std::array<CRGB, LED_LIMIT> leds = {};

size_t currentLeds = 0;

std::vector<String> shaders = {};
std::vector<LuaAnimation *> loadedAnimations = {};

uint16_t currentAnimationShaderIndex = 0;
LuaAnimation *currentAnimation = nullptr;
long lastUpdate = 0;

bool toReload = false;

uint32_t animationTime = 0;
uint32_t animationIteration = 0;

void setCurrentAnimation(LuaAnimation *animation) {
    currentAnimation = animation;
    String animationName;

    if (animation != nullptr) {
        animationName = animation->getName();
    } else {
        animationName = "";
    }

    ShaderStorage::get().saveLastShader(animationName);

    SocketController::animationSelected(animationName);
}

CallResult<LuaAnimation *> loadCached(String &shaderName) {
    for (auto anim : loadedAnimations) {
        if (anim->getName() == shaderName) {
            return anim;
        }
    }

    Serial.printf("Loading shader \"%s\"\n", shaderName.c_str());
    CallResult<String> shaderResult = ShaderStorage::get().getShader(shaderName);
    if (shaderResult.hasError()) {
        return CallResult<LuaAnimation *>(nullptr, shaderResult.getCode(), shaderResult.getMessage().c_str());
    }
    String shader = shaderResult.getValue();

    LuaAnimation *animation = new LuaAnimation(shaderName);
    CallResult<void *> beginResult = animation->begin(shader);
    if (beginResult.hasError()) {
        delete animation;
        return CallResult<LuaAnimation *>(nullptr, beginResult.getCode(), beginResult.getMessage().c_str());
    }

    loadedAnimations.push_back(animation);
    if (loadedAnimations.size() > CACHE_SIZE) {
        LuaAnimation *toRemove = loadedAnimations[0];
        loadedAnimations.erase(loadedAnimations.begin());
        delete toRemove;
    }

    return CallResult<LuaAnimation *>(animation, 200);
}

CallResult<void *> reload() {
    Serial.println("Performing cache cleanup");
    for (auto anim : loadedAnimations) {
        delete anim;
    }
    loadedAnimations.clear();
    shaders.clear();

    CallResult<std::vector<String>> shadersResult = ShaderStorage::get().listShaders();
    if (shadersResult.hasError()) {
        return CallResult<void *>(nullptr, shadersResult.getCode(), shadersResult.getMessage().c_str());
    }
    shaders = shadersResult.getValue();
    if (shaders.size() == 0) {
        Serial.println("no shaders loaded");
        currentAnimationShaderIndex = 0;
        setCurrentAnimation(nullptr);
        return CallResult<void *>(nullptr, 200);
    }
    String savedShader = ShaderStorage::get().getLastShader();
    bool saveLoaded = false;
    if (savedShader != "") {
        CallResult<void *> result = Anime::select(savedShader);
        Serial.print("saved loading: ");
        Serial.print(result.hasError());
        if (!result.hasError()) {
            saveLoaded = true;
            Serial.println();
        } else {
            Serial.println(result.getMessage());
        }
    }

    if (!saveLoaded) {
        if (currentAnimationShaderIndex >= shaders.size()) {
            currentAnimationShaderIndex = shaders.size() - 1;
        }

        CallResult<LuaAnimation *> loadResult = loadCached(shaders[currentAnimationShaderIndex]);
        if (loadResult.hasError()) {
            return CallResult<void *>(nullptr, loadResult.getCode(), loadResult.getMessage().c_str());
        }
        setCurrentAnimation(loadResult.getValue());
    }

    Serial.println("Shaders reload finished");
    return CallResult<void *>(nullptr, 200);
}

}  // namespace

namespace Anime {

CallResult<void *> connect() {
    currentLeds = ShaderStorage::get().getProperty("activeLeds", String(LED_LIMIT)).toInt();

    CallResult<void *> loadResult = reload();
    if (loadResult.hasError()) {
        return loadResult;
    }

    FastLED.addLeds<LED_MODEL, LED_PIN, RGB>(leds.data(), LED_LIMIT).setCorrection(TypicalSMD5050);
    FastLED.setBrightness(255);
    FastLED.clear(true);
    return CallResult<void *>(nullptr);
}

CallResult<void *> select(String &shaderName) {
    uint16_t shaderSize = shaders.size();
    uint16_t foundShaderIndex = 0;
    bool notFound = true;
    for (uint16_t i = 0; i < shaderSize; i++) {
        if (shaders[i] == shaderName) {
            foundShaderIndex = i;
            notFound = false;
            break;
        }
    }
    if (notFound) {
        return CallResult<void *>(nullptr, 404, "No such shader");
    }
    currentAnimationShaderIndex = foundShaderIndex;
    CallResult<LuaAnimation *> loadResult = loadCached(shaders[currentAnimationShaderIndex]);
    if (loadResult.hasError()) {
        return CallResult<void *>(nullptr, loadResult.getCode(), loadResult.getMessage().c_str());
    }
    setCurrentAnimation(loadResult.getValue());
    return CallResult<void *>(nullptr, 200);
}

CallResult<void *> draw() {
    Anime::sampleTime();
    Anime::incIter();

    if (toReload) {
        CallResult<void *> reloadResult = reload();
        if (reloadResult.hasError()) {
            return reloadResult;
        }
        toReload = false;
    }

    if (currentAnimation == nullptr) {
        FastLED.clear(true);
        lastUpdate = millis();
    } else {
        CallResult<void *> result = currentAnimation->apply(leds.data(), currentLeds);

        if (result.hasError()) {
            return result;
        }
        FastLED.show();
        lastUpdate = millis();
    }

    return CallResult<void *>(nullptr, 200);
}

void scheduleReload() { toReload = true; }

size_t getCurrentLeds() { return currentLeds; }

void setCurrentLeds(size_t acurrentLeds) {
    currentLeds = acurrentLeds;
    for (int i = currentLeds; i < LED_LIMIT; i++) {
        leds[i] = CRGB(0, 0, 0);
    }
    ShaderStorage::get().saveProperty("activeLeds", String(currentLeds));
}

String getCurrent() {
    if (currentAnimation == nullptr) {
        return "";
    } else {
        return currentAnimation->getName();
    }
}

uint32_t getTime() { return animationTime; }
uint32_t getIter() { return animationIteration; }

void sampleTime() { animationTime = millis(); }
void incIter() { animationIteration++; }
}  // namespace Anime