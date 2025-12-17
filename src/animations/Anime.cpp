#include "Anime.h"

#include <Arduino.h>
#include <esp_sleep.h>

#include "web/SocketController.h"
#include "PropertySystem.h"
#include "Property.h"

namespace {

std::array<CRGB, LED_LIMIT> leds = {};

size_t currentLeds = 0;

// Persistent property with automatic debounced saving (1 second default)
MicroProto::Property<uint8_t> brightness("brightness", 255, MicroProto::PropertyLevel::LOCAL,
    MicroProto::Constraints<uint8_t>().min(0).max(255),
    "LED output brightness",  // description
    MicroProto::UIHints(),    // UI hints (default)
    true);                    // persistent=true

std::vector<String> shaders = {};
std::vector<LuaAnimation *> loadedAnimations = {};

uint16_t currentAnimationShaderIndex = 0;
LuaAnimation *currentAnimation = nullptr;
long lastUpdate = 0;

bool toReload = false;

uint32_t animationTime = 0;
uint32_t animationIteration = 0;
float deltaTime = 0;

uint32_t lastSocketSendMillist = 0;

// Power saving
uint32_t lastNonBlackTime = 0;
uint32_t startupTime = 0;  // Track when system started
bool inPowerSaveMode = false;
const uint32_t POWER_SAVE_TIMEOUT = 60000;  // 1 minute
const uint32_t STARTUP_GRACE_PERIOD = 5 * 60 * 1000;  // 5 minutes grace period after startup

// Atmospheric fade (like kerosene lamp running out)
bool atmosphericFadeEnabled = false;
uint32_t lastFadeUpdate = 0;
const uint32_t FADE_INTERVAL = 30 * 1000;  // 30 seconds between brightness decrements (255 * 30s = 127.5 min ≈ 2 hours)

bool areAllLedsBlack() {
    for (size_t i = 0; i < currentLeds; i++) {
        if (leds[i].r != 0 || leds[i].g != 0 || leds[i].b != 0) {
            return false;
        }
    }
    return true;
}

void enterPowerSaveMode() {
    if (inPowerSaveMode) return;

    Serial.println("[Anime] Entering power save mode with light sleep");
    inPowerSaveMode = true;
    FastLED.clear(true);
}

void exitPowerSaveMode() {
    if (!inPowerSaveMode) return;

    Serial.println("[Anime] Waking from power save mode");
    inPowerSaveMode = false;
    lastNonBlackTime = millis();
}

void updateAtmosphericFade() {
    if (!atmosphericFadeEnabled) return;
    if (millis() - lastFadeUpdate < FADE_INTERVAL) return;

    lastFadeUpdate = millis();
    uint8_t current = brightness.get();
    if (current > 0) {
        brightness = current - 1;
        Serial.printf("[Anime] Atmospheric fade: brightness reduced to %d\n", current - 1);
    }
}

void sendLedsToSocket() {
    // Temporarily disabled to test network reliability
    return;

    if (millis() - lastSocketSendMillist < 100) {
        return;
    }

    SocketController::updateLedVals(leds.data(), currentLeds);
    lastSocketSendMillist = millis();
}

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

    Serial.printf("Loading shader \"%s\", Free: %d bytes\n", shaderName.c_str(), ESP.getFreeHeap());
    CallResult<String> shaderResult = ShaderStorage::get().getShader(shaderName);
    if (shaderResult.hasError()) {
        return CallResult<LuaAnimation *>(nullptr, shaderResult.getCode(), shaderResult.getMessage().c_str());
    }
    Serial.printf("  After getShader, Free: %d bytes\n", ESP.getFreeHeap());

    String shader = shaderResult.getValue();
    Serial.printf("  After String copy (len=%d), Free: %d bytes\n", shader.length(), ESP.getFreeHeap());

    LuaAnimation *animation = new LuaAnimation(shaderName);
    Serial.printf("  After new LuaAnimation, Free: %d bytes\n", ESP.getFreeHeap());

    CallResult<void *> beginResult = animation->begin(shader);
    Serial.printf("  After animation->begin, Free: %d bytes\n", ESP.getFreeHeap());

    if (beginResult.hasError()) {
        delete animation;
        return CallResult<LuaAnimation *>(nullptr, beginResult.getCode(), beginResult.getMessage().c_str());
    }

    loadedAnimations.push_back(animation);
    if (loadedAnimations.size() > CACHE_SIZE) {
        LuaAnimation *toRemove = loadedAnimations[0];
        loadedAnimations.erase(loadedAnimations.begin());
        Serial.printf("  Deleting old animation, Free before: %d bytes\n", ESP.getFreeHeap());
        delete toRemove;
        Serial.printf("  After delete, Free: %d bytes\n", ESP.getFreeHeap());
    }

    Serial.printf("  Before return (shader String destroyed), Free: %d bytes\n", ESP.getFreeHeap());
    return CallResult<LuaAnimation *>(animation, 200);
}

CallResult<void *> setAnimationByIndex(uint16_t shaderIndex) {
    Serial.printf("setAnimationByIndex start, Free: %d bytes\n", ESP.getFreeHeap());
    currentAnimationShaderIndex = shaderIndex;
    CallResult<LuaAnimation *> loadResult = loadCached(shaders[currentAnimationShaderIndex]);
    Serial.printf("After loadCached return, Free: %d bytes\n", ESP.getFreeHeap());
    if (loadResult.hasError()) {
        return CallResult<void *>(nullptr, loadResult.getCode(), loadResult.getMessage().c_str());
    }
    setCurrentAnimation(loadResult.getValue());
    Serial.printf("After setCurrentAnimation, Free: %d bytes\n", ESP.getFreeHeap());
    return CallResult<void *>(nullptr, 200); 
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

        auto result = setAnimationByIndex(currentAnimationShaderIndex);
        if (result.hasError()) {
            return result;
        }

        // CallResult<LuaAnimation *> loadResult = loadCached(shaders[currentAnimationShaderIndex]);
        // if (loadResult.hasError()) {
        //     return CallResult<void *>(nullptr, loadResult.getCode(), loadResult.getMessage().c_str());
        // }
        // setCurrentAnimation(loadResult.getValue());
    }

    Serial.println("Shaders reload finished");
    return CallResult<void *>(nullptr, 200);
}

}  // namespace

namespace Anime {

CallResult<void *> connect() {
    // Record startup time for grace period
    startupTime = millis();

    int cl = ShaderStorage::get().getProperty("activeLeds", String(LED_LIMIT)).toInt();
    currentLeds = std::min(200, std::max(0, cl));

    CallResult<void *> loadResult = reload();
    if (loadResult.hasError()) {
        return loadResult;
    }

    FastLED.addLeds<LED_MODEL, LED_PIN, RGB_ORDER>(leds.data(), LED_LIMIT).setCorrection(TypicalSMD5050);
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
    deltaTime = (millis() - animationTime) / 1000.0;
    Anime::sampleTime();
    Anime::incIter();

    // Use light sleep in power save mode
    if (inPowerSaveMode) {
        // Enter light sleep for 500ms (or until GPIO wakeup)
        // WiFi and network connections remain active in light sleep
        esp_sleep_enable_timer_wakeup(500000);  // 500ms in microseconds
        esp_light_sleep_start();
        return CallResult<void *>(nullptr, 200);
    }

    if (toReload) {
        CallResult<void *> reloadResult = reload();
        if (reloadResult.hasError()) {
            return reloadResult;
        }
        toReload = false;
    }

    // Update atmospheric fade (reduce brightness gradually)
    updateAtmosphericFade();

    if (currentAnimation == nullptr) {
        FastLED.clear(true);
    } else {
        CallResult<void *> result = currentAnimation->apply(leds.data(), currentLeds);

        if (result.hasError()) {
            return result;
        }
        FastLED.show();
    }

    // Check for power saving (but skip during startup grace period)
    uint32_t currentTime = millis();
    bool inGracePeriod = (currentTime - startupTime) < STARTUP_GRACE_PERIOD;

    if (areAllLedsBlack()) {
        if (lastNonBlackTime == 0) {
            lastNonBlackTime = currentTime;
        } else if (!inGracePeriod && (currentTime - lastNonBlackTime > POWER_SAVE_TIMEOUT)) {
            enterPowerSaveMode();
        }
    } else {
        lastNonBlackTime = currentTime;
    }

    sendLedsToSocket();
    lastUpdate = millis();

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

uint8_t getBrightness() {
    return brightness.get();
}

void setBrightness(uint8_t newBrightness) {
    brightness = newBrightness;
}

String getCurrent() {
    if (currentAnimation == nullptr) {
        return "";
    } else {
        return currentAnimation->getName();
    }
}

void nextAnimation() {
    if (shaders.empty()) return;
    currentAnimationShaderIndex = (currentAnimationShaderIndex + 1) % shaders.size();
    setAnimationByIndex(currentAnimationShaderIndex);
}

void previousAnimation() {
    if (shaders.empty()) return;
    if (currentAnimationShaderIndex == 0) {
        currentAnimationShaderIndex = shaders.size() - 1;
    } else {
        currentAnimationShaderIndex--;
    }
    setAnimationByIndex(currentAnimationShaderIndex);
}

uint32_t getTime() { return animationTime; }
uint32_t getIter() { return animationIteration; }
float getDeltaTime() { return deltaTime; }

void sampleTime() { animationTime = millis(); }
void incIter() { animationIteration++; }

void wakeUp() {
    exitPowerSaveMode();
}

void enableAtmosphericFade() {
    atmosphericFadeEnabled = true;
    lastFadeUpdate = millis();
    Serial.println("[Anime] Atmospheric fade enabled");
}

void disableAtmosphericFade() {
    atmosphericFadeEnabled = false;
    Serial.println("[Anime] Atmospheric fade disabled");
}

bool isAtmosphericFadeEnabled() {
    return atmosphericFadeEnabled;
}

}  // namespace Anime