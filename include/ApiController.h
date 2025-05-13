#ifndef API_CONTROLLER_H
#define API_CONTROLLER_H

#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>

#include "ShaderStorage.h"
#include "AnimationManager.h"

class ApiController {
public:
    ApiController(AnimationManager *animationManager);
    void onAddShader(AsyncWebServerRequest *request, JsonVariant &json);
    void onListShaders(AsyncWebServerRequest *request);
    void onGetShader(String& shader, AsyncWebServerRequest *request);
    void onDeleteShader(String& shader, AsyncWebServerRequest *request);

    void onShow(String& shader, AsyncWebServerRequest *request);
    void onGetShow(AsyncWebServerRequest *request);
private:
    AnimationManager *animationManager;
};

#endif //API_CONTROLLER_H