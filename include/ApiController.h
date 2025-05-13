#ifndef API_CONTROLLER_H
#define API_CONTROLLER_H

#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>

#include "ShaderStorage.h"
#include "Anime.h"

class ApiController {
public:
    void onAddShader(AsyncWebServerRequest *request, JsonVariant &json);
    void onListShaders(AsyncWebServerRequest *request);
    void onGetShader(String& shader, AsyncWebServerRequest *request);
    void onDeleteShader(String& shader, AsyncWebServerRequest *request);

    void onShow(String& shader, AsyncWebServerRequest *request);
    void onGetShow(AsyncWebServerRequest *request);
};

#endif //API_CONTROLLER_H