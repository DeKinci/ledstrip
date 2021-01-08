#ifndef API_CONTROLLER_H
#define API_CONTROLLER_H

#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>

#include "ShaderStorage.h"
#include "AnimationManager.h"

class ApiController {
public:
    ApiController(ShaderStorage* shaderStorage, AnimationManager *animationManager);
    void onAddShader(AsyncWebServerRequest *request, JsonVariant &json);
    void onListShaders(AsyncWebServerRequest *request);
    void onGetShader(String& shader, AsyncWebServerRequest *request);
    void onDeleteShader(String& shader, AsyncWebServerRequest *request);

    void onShow(String& shader, AsyncWebServerRequest *request);

private:
    ShaderStorage* shaderStorage;
    AnimationManager *animationManager;
};

#endif //API_CONTROLLER_H