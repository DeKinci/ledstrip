#include "AnimationManager.h"

AnimationManager::AnimationManager(GlobalAnimationEnv* globalAnimationEnv, size_t ledLimit) {
    this->globalAnimationEnv = globalAnimationEnv;
    this->ledLimit = ledLimit;
    this->leds = new CRGB[ledLimit];
    this->currentLeds = ShaderStorage::get().getProperty("activeLeds", String(ledLimit)).toInt();
}

AnimationManager::~AnimationManager() {
    for (auto anim : *loadedAnimations) {
        delete anim;
    }
    delete loadedAnimations;
    delete shaders;
    delete leds;
}

void AnimationManager::previous() {
}

void AnimationManager::next() {
}

void AnimationManager::faster() {
    if (speed <= 90)
        speed += 10;
}

void AnimationManager::slower() {
    if (speed >= 10)
        speed -= 10;
}

CallResult<void*> AnimationManager::select(String& shaderName) {
    uint16_t shaderSize = shaders->size();
    uint16_t foundShaderIndex = 0;
    bool notFound = true;
    for (uint16_t i = 0; i < shaderSize; i++) {
        if ((*shaders)[i] == shaderName) {
            foundShaderIndex = i;
            notFound = false;
            break;
        }
    }
    if (notFound) {
        return CallResult<void*>(nullptr, 404, "No such shader");
    }
    currentAnimationShaderIndex = foundShaderIndex;
    CallResult<LuaAnimation*> loadResult = loadCached((*shaders)[currentAnimationShaderIndex]);
    if (loadResult.hasError()) {
        return CallResult<void*>(nullptr, loadResult.getCode(), loadResult.getMessage().c_str());
    }
    setCurrentAnimation(loadResult.getValue());
    return CallResult<void*>(nullptr, 200);
}

CallResult<void*> AnimationManager::draw() {
    if (leds == nullptr) {
        return CallResult<void*>(nullptr, 500, "Leds were not connected programmaticaly");
    }

    if (toReload) {
        CallResult<void*> reloadResult = reload();
        if (reloadResult.hasError()) {
            return reloadResult;
        }
        toReload = false;
    }

    if (currentAnimation == nullptr) {
        FastLED.clear(true);
        lastUpdate = millis();
    } else {
        CallResult<void*> result = currentAnimation->apply(leds, currentLeds);

        if (result.hasError()) {
            Serial.print("Apply animation error ");
            Serial.println(result.getMessage());
        }
        FastLED.show();
        lastUpdate = millis();
    }

    return CallResult<void*>(nullptr, 200);
}

void AnimationManager::scheduleReload() {
    toReload = true;
}

size_t AnimationManager::getCurrentLeds() const {
    return currentLeds;
}

void AnimationManager::setCurrentLeds(size_t currentLeds) {
    this->currentLeds = currentLeds;
    for (int i = currentLeds; i < ledLimit; i++) {
        leds[i] = CRGB(0, 0, 0);
    }
}

CallResult<void*> AnimationManager::reload() {
    Serial.println("Performing cache cleanup");
    for (auto anim : *loadedAnimations) {
        delete anim;
    }
    loadedAnimations->clear();
    delete shaders;

    CallResult<std::vector<String>*> shadersResult = ShaderStorage::get().listShaders();
    if (shadersResult.hasError()) {
        return CallResult<void*>(nullptr, shadersResult.getCode(), shadersResult.getMessage().c_str());
    }
    shaders = shadersResult.getValue();
    if (shaders->size() == 0) {
        Serial.println("no shaders loaded");
        currentAnimationShaderIndex = 0;
        setCurrentAnimation(nullptr);
        return CallResult<void*>(nullptr, 200);
    }
    String savedShader = ShaderStorage::get().getLastShader();
    bool saveLoaded = false;
    if (savedShader != "") {
        CallResult<void*> result = select(savedShader);
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
        if (currentAnimationShaderIndex >= shaders->size()) {
            currentAnimationShaderIndex = shaders->size() - 1;
        }

        CallResult<LuaAnimation*> loadResult = loadCached((*shaders)[currentAnimationShaderIndex]);
        if (loadResult.hasError()) {
            return CallResult<void*>(nullptr, loadResult.getCode(), loadResult.getMessage().c_str());
        }
        setCurrentAnimation(loadResult.getValue());
    }

    Serial.println("Shaders reload finished");
    return CallResult<void*>(nullptr, 200);
}

CallResult<LuaAnimation*> AnimationManager::loadCached(String& shaderName) {
    for (auto anim : *loadedAnimations) {
        if (anim->getName() == shaderName) {
            return anim;
        }
    }

    Serial.printf("Loading shader \"%s\"\n", shaderName.c_str());
    CallResult<String> shaderResult = ShaderStorage::get().getShader(shaderName);
    if (shaderResult.hasError()) {
        return CallResult<LuaAnimation*>(nullptr, shaderResult.getCode(), shaderResult.getMessage().c_str());
    }
    String shader = shaderResult.getValue();

    LuaAnimation* animation = new LuaAnimation(shaderName);
    CallResult<void*> beginResult = animation->begin(shader, globalAnimationEnv);
    if (beginResult.hasError()) {
        delete animation;
        return CallResult<LuaAnimation*>(nullptr, beginResult.getCode(), beginResult.getMessage().c_str());
    }

    loadedAnimations->push_back(animation);
    if (loadedAnimations->size() > CACHE_SIZE) {
        LuaAnimation* toRemove = (*loadedAnimations)[0];
        loadedAnimations->erase(loadedAnimations->begin());
        delete toRemove;
    }

    return CallResult<LuaAnimation*>(animation, 200);
}

String AnimationManager::getCurrent() {
    if (currentAnimation == nullptr) {
        return "";
    } else {
        return currentAnimation->getName();
    }
}

void AnimationManager::setListener(SelectAnimationListener* listener) {
    AnimationManager::listener = listener;
}

void AnimationManager::setCurrentAnimation(LuaAnimation* animation) {
    currentAnimation = animation;
    String animationName;

    if (animation != nullptr) {
        animationName = animation->getName();
    } else {
        animationName = "";
    }

    ShaderStorage::get().saveLastShader(animationName);

    if (listener != nullptr) {
        listener->animationSelected(animationName);
    }
}
