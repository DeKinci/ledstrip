#ifndef API_CONTROLLER_H
#define API_CONTROLLER_H

#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>

#include "Anime.h"
#include "ShaderStorage.h"

namespace ApiController {
void onAddShader(AsyncWebServerRequest *request, JsonVariant &json);
void onListShaders(AsyncWebServerRequest *request);
void onGetShader(String &shader, AsyncWebServerRequest *request);
void onDeleteShader(String &shader, AsyncWebServerRequest *request);

void onShow(String &shader, AsyncWebServerRequest *request);
void onGetShow(AsyncWebServerRequest *request);
};  // namespace ApiController

#endif  // API_CONTROLLER_H