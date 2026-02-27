#include "Anime.h"

#include <Arduino.h>
#include <esp_sleep.h>
#include <Logger.h>

#include "PropertySystem.h"
#include "Property.h"
#include "ListProperty.h"
#include "ResourceProperty.h"

static const char* TAG = "Anime";

namespace {

std::array<CRGB, LED_LIMIT> leds = {};

size_t currentLeds = 0;

// Persistent property with automatic debounced saving (1 second default)
MicroProto::Property<uint8_t> brightness("brightness", 255, MicroProto::PropertyLevel::LOCAL,
    MicroProto::Constraints<uint8_t>().min(0).max(255),
    "LED output brightness",
    MicroProto::UIHints().setColor(MicroProto::UIColor::AMBER).setIcon("💡").setUnit("%"),
    true);  // persistent

// Animation selection (index into shader list)
MicroProto::Property<uint8_t> shaderIndex("shaderIndex", 0, MicroProto::PropertyLevel::LOCAL,
    MicroProto::Constraints<uint8_t>().min(0).max(255),
    "Current animation index",
    MicroProto::UIHints().setColor(MicroProto::UIColor::CYAN).setIcon("🎬"),
    true);  // persistent

// Active LED count
MicroProto::Property<uint8_t> ledCount("ledCount", LED_LIMIT, MicroProto::PropertyLevel::LOCAL,
    MicroProto::Constraints<uint8_t>().min(1).max(LED_LIMIT),
    "Number of active LEDs",
    MicroProto::UIHints().setColor(MicroProto::UIColor::LIME).setIcon("💡"),
    true);  // persistent

// Atmospheric fade toggle
MicroProto::Property<bool> atmosphericFadeProp("atmosphericFade", false, MicroProto::PropertyLevel::LOCAL,
    "Gradual brightness fade (kerosene lamp effect)",
    MicroProto::UIHints().setColor(MicroProto::UIColor::ORANGE).setIcon("🕯️"),
    true);  // persistent

// LED RGB preview stream (3 bytes per LED: R, G, B, R, G, B, ...)
MicroProto::ListProperty<uint8_t, LED_LIMIT * 3> ledPreview("ledPreview", {}, MicroProto::PropertyLevel::LOCAL,
    MicroProto::ListConstraints<uint8_t>().maxLength(LED_LIMIT * 3),
    "Live LED preview RGB values",
    MicroProto::UIHints().setColor(MicroProto::UIColor::PINK).setIcon("🌈"));

// Shader resources: header = name (bytes, max 64), body = Lua code (bytes)
// Max 16 shaders, headers sync automatically via PROPERTY_UPDATE
// Using TYPE_LIST as a byte array (strings are LIST<UINT8> in wire format)
MicroProto::ResourceProperty<16, 64> shadersResource(
    "shaders",
    MicroProto::ResourceTypeDef(MicroProto::TYPE_LIST, 64),   // header = name (byte array)
    MicroProto::ResourceTypeDef(MicroProto::TYPE_LIST, 0),    // body = Lua code (byte array, variable length)
    MicroProto::PropertyLevel::LOCAL,
    "Animation shaders",
    MicroProto::UIHints().setColor(MicroProto::UIColor::CYAN).setIcon("🎨"),
    true,   // persistent
    false,  // not hidden
    false,  // not BLE exposed
    0       // group_id
);

// Mapping from index to resourceId (rebuilt on reload)
std::vector<uint32_t> shaderResourceIds = {};
std::vector<String> shaders = {};
std::vector<LuaAnimation *> loadedAnimations = {};

uint16_t currentAnimationShaderIndex = 0;
LuaAnimation *currentAnimation = nullptr;
long lastUpdate = 0;

bool toReload = false;

uint32_t animationTime = 0;
uint32_t animationIteration = 0;
float deltaTime = 0;

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

    LOG_INFO(TAG, "Entering power save mode with light sleep");
    inPowerSaveMode = true;
    FastLED.clear(true);
}

void exitPowerSaveMode() {
    if (!inPowerSaveMode) return;

    LOG_INFO(TAG, "Waking from power save mode");
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
        LOG_INFO(TAG, "Atmospheric fade: brightness reduced to %d", current - 1);
    }
}

void updateLedPreview() {
    static uint32_t lastUpdate = 0;
    if (millis() - lastUpdate < 100) return;  // 10 Hz
    lastUpdate = millis();

    // Update preview with current LED RGB values
    ledPreview.resize(currentLeds * 3);
    for (size_t i = 0; i < currentLeds; i++) {
        ledPreview.set(i * 3, leds[i].r);
        ledPreview.set(i * 3 + 1, leds[i].g);
        ledPreview.set(i * 3 + 2, leds[i].b);
    }
}

void setCurrentAnimation(LuaAnimation *animation) {
    currentAnimation = animation;
    // shaderIndex property is persisted and will be restored on reboot
    // No need to save animation name separately
}

CallResult<LuaAnimation *> loadCached(uint16_t index) {
    if (index >= shaders.size()) {
        return CallResult<LuaAnimation *>(nullptr, 404, "Shader index out of range");
    }

    String shaderName = shaders[index];
    uint32_t resourceId = shaderResourceIds[index];

    // Check if already loaded
    for (auto anim : loadedAnimations) {
        if (anim->getName() == shaderName) {
            return anim;
        }
    }

    LOG_INFO(TAG, "Loading shader \"%s\" (id=%lu), Free: %d bytes", shaderName.c_str(), resourceId, ESP.getFreeHeap());

    // Read shader body from ResourceProperty
    size_t bodySize = shadersResource.getBodySize(resourceId);
    if (bodySize == 0) {
        return CallResult<LuaAnimation *>(nullptr, 404, "Shader body not found");
    }

    // Allocate buffer for shader code
    std::vector<uint8_t> buffer(bodySize + 1);
    size_t bytesRead = shadersResource.readBody(resourceId, buffer.data(), bodySize);
    if (bytesRead == 0) {
        // Body file missing (e.g., after path format change) - clean up orphaned header
        LOG_WARN(TAG, "Shader body file missing, removing orphaned header for id=%lu", resourceId);
        shadersResource.deleteResource(resourceId);
        return CallResult<LuaAnimation *>(nullptr, 404, "Shader body file missing (cleaned up)");
    }
    buffer[bytesRead] = '\0';  // Null-terminate

    LOG_DEBUG(TAG, "  After readBody (%d bytes), Free: %d bytes", bytesRead, ESP.getFreeHeap());

    String shader = String(reinterpret_cast<char*>(buffer.data()));
    LOG_DEBUG(TAG, "  After String copy (len=%d), Free: %d bytes", shader.length(), ESP.getFreeHeap());

    LuaAnimation *animation = new LuaAnimation(shaderName);
    LOG_DEBUG(TAG, "  After new LuaAnimation, Free: %d bytes", ESP.getFreeHeap());

    CallResult<void *> beginResult = animation->begin(shader);
    LOG_DEBUG(TAG, "  After animation->begin, Free: %d bytes", ESP.getFreeHeap());

    if (beginResult.hasError()) {
        delete animation;
        return CallResult<LuaAnimation *>(nullptr, beginResult.getCode(), beginResult.getMessage().c_str());
    }

    loadedAnimations.push_back(animation);
    if (loadedAnimations.size() > CACHE_SIZE) {
        LuaAnimation *toRemove = loadedAnimations[0];
        loadedAnimations.erase(loadedAnimations.begin());
        LOG_DEBUG(TAG, "  Deleting old animation, Free before: %d bytes", ESP.getFreeHeap());
        delete toRemove;
        LOG_DEBUG(TAG, "  After delete, Free: %d bytes", ESP.getFreeHeap());
    }

    LOG_DEBUG(TAG, "  Before return (shader String destroyed), Free: %d bytes", ESP.getFreeHeap());
    return CallResult<LuaAnimation *>(animation, 200);
}

CallResult<void *> setAnimationByIndex(uint16_t shaderIdx) {
    LOG_INFO(TAG, "setAnimationByIndex start, Free: %d bytes", ESP.getFreeHeap());
    currentAnimationShaderIndex = shaderIdx;
    CallResult<LuaAnimation *> loadResult = loadCached(currentAnimationShaderIndex);
    LOG_DEBUG(TAG, "After loadCached return, Free: %d bytes", ESP.getFreeHeap());
    if (loadResult.hasError()) {
        return CallResult<void *>(nullptr, loadResult.getCode(), loadResult.getMessage().c_str());
    }
    setCurrentAnimation(loadResult.getValue());
    LOG_DEBUG(TAG, "After setCurrentAnimation, Free: %d bytes", ESP.getFreeHeap());
    return CallResult<void *>(nullptr, 200);
}

CallResult<void *> reload() {
    LOG_INFO(TAG, "Performing cache cleanup");
    for (auto anim : loadedAnimations) {
        delete anim;
    }
    loadedAnimations.clear();
    shaders.clear();
    shaderResourceIds.clear();

    // Build shader list from ResourceProperty headers
    shadersResource.forEach([](uint32_t id, const MicroProto::ResourceHeader& header, const void* headerData) {
        const char* name = static_cast<const char*>(headerData);
        shaders.push_back(String(name));
        shaderResourceIds.push_back(id);
        LOG_INFO(TAG, "  Found shader: %s (id=%lu, size=%lu)", name, id, header.bodySize);
        return true;  // Continue iteration
    });

    LOG_INFO(TAG, "Loaded %d shaders from ResourceProperty", shaders.size());

    if (shaders.size() == 0) {
        LOG_INFO(TAG, "No shaders found, creating defaults");

        struct DefaultShader { const char* name; const char* code; };
        static const DefaultShader defaults[] = {
            {"rainbow",
                "function draw(n)\n"
                "  for i=0,n-1 do\n"
                "    hsv(i, env.millis/10 + i*5, 1, env.brightness)\n"
                "  end\n"
                "end"
            },
            {"breathe",
                "function draw(n)\n"
                "  local b = (math.sin(env.millis/1000) + 1) * 0.5\n"
                "  for i=0,n-1 do\n"
                "    hsv(i, 0, 0, b * env.brightness)\n"
                "  end\n"
                "end"
            },
            {"fire",
                "function draw(n)\n"
                "  for i=0,n-1 do\n"
                "    local flicker = math.random(100, 255) / 255\n"
                "    hsv(i, math.random(0, 30), 1, flicker * env.brightness)\n"
                "  end\n"
                "end"
            },
            {"comet",
                "function draw(n)\n"
                "  local pos = (env.millis / 20) % n\n"
                "  for i=0,n-1 do\n"
                "    local dist = (i - pos) % n\n"
                "    local tail = math.max(0, 1 - dist / 8.5)\n"
                "    hsv(i, 160, 1, tail * env.brightness)\n"
                "  end\n"
                "end"
            },
        };

        for (const auto& s : defaults) {
            uint32_t id = shadersResource.createResource(
                s.name, strlen(s.name),
                s.code, strlen(s.code)
            );
            if (id > 0) {
                LOG_INFO(TAG, "Created default shader: %s (id=%lu)", s.name, id);
                shaders.push_back(String(s.name));
                shaderResourceIds.push_back(id);
            }
        }

        if (shaders.size() == 0) {
            LOG_WARN(TAG, "Failed to create any default shaders");
            currentAnimationShaderIndex = 0;
            setCurrentAnimation(nullptr);
            return CallResult<void *>(nullptr, 200);
        }
    }

    // Try to restore last animation by index (persisted via shaderIndex property)
    if (currentAnimationShaderIndex >= shaders.size()) {
        currentAnimationShaderIndex = shaders.size() > 0 ? shaders.size() - 1 : 0;
    }

    // Try to load, but don't fail startup if shader is broken
    if (shaders.size() > 0) {
        auto result = setAnimationByIndex(currentAnimationShaderIndex);
        if (result.hasError()) {
            LOG_ERROR(TAG, "Failed to load shader %d: %s (continuing without animation)",
                      currentAnimationShaderIndex, result.getMessage().c_str());
            setCurrentAnimation(nullptr);
            // Don't return error - just run without animation
        }
    }

    LOG_INFO(TAG, "Shaders reload finished");
    return CallResult<void *>(nullptr, 200);
}

}  // namespace

namespace Anime {

CallResult<void *> connect() {
    // Record startup time for grace period
    startupTime = millis();

    // Wire property change callbacks
    shaderIndex.onChangeTyped([](uint8_t, uint8_t newIdx) {
        if (newIdx < shaders.size()) {
            setAnimationByIndex(newIdx);
        }
    });

    ledCount.onChangeTyped([](uint8_t, uint8_t newCount) {
        currentLeds = newCount;
        for (size_t i = newCount; i < LED_LIMIT; i++) {
            leds[i] = CRGB(0, 0, 0);
        }
    });

    shadersResource.onChange([]() {
        LOG_INFO(TAG, "Shaders changed, reloading...");
        reload();
    });

    atmosphericFadeProp.onChangeTyped([](bool, bool enabled) {
        atmosphericFadeEnabled = enabled;
        if (enabled) {
            lastFadeUpdate = millis();
            LOG_INFO(TAG, "Atmospheric fade enabled");
        } else {
            LOG_INFO(TAG, "Atmospheric fade disabled");
        }
    });

    // Load initial LED count from property (handles persistence)
    currentLeds = ledCount.get();

    CallResult<void *> loadResult = reload();
    if (loadResult.hasError()) {
        return loadResult;
    }

    FastLED.addLeds<LED_MODEL, LED_PIN, RGB_ORDER>(leds.data(), LED_LIMIT).setCorrection(TypicalSMD5050);
#if defined(MAX_POWER_MW) && MAX_POWER_MW > 0
    FastLED.setMaxPowerInMilliWatts(MAX_POWER_MW);
#endif
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
    CallResult<LuaAnimation *> loadResult = loadCached(currentAnimationShaderIndex);
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
        // Detach RMT from LED pin before sleep — the RMT peripheral
        // can output garbage on the data line during wake-up, causing
        // random color flashes. Reclaiming the pin as plain GPIO LOW
        // ensures a clean signal regardless of RMT state.
        pinMode(LED_PIN, OUTPUT);
        digitalWrite(LED_PIN, LOW);

        esp_sleep_enable_timer_wakeup(500000);  // 500ms
        esp_light_sleep_start();

        // RMT reclaims the pin on next FastLED.show()/clear()
        FastLED.clear(true);
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

    updateLedPreview();
    lastUpdate = millis();

    return CallResult<void *>(nullptr, 200);
}

void scheduleReload() { toReload = true; }

size_t getCurrentLeds() { return currentLeds; }

void setCurrentLeds(size_t newCount) {
    // Use property which triggers callback and handles persistence
    ledCount = static_cast<uint8_t>(newCount);
}

size_t getShaderCount() {
    return shaders.size();
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
    LOG_INFO(TAG, "Atmospheric fade enabled");
}

void disableAtmosphericFade() {
    atmosphericFadeEnabled = false;
    LOG_INFO(TAG, "Atmospheric fade disabled");
}

bool isAtmosphericFadeEnabled() {
    return atmosphericFadeEnabled;
}

}  // namespace Anime