#include "ApiController.h"
#include <ArduinoJson.h>
#include <AsyncJson.h>

ApiController::ApiController(ShaderStorage* shaderStorage, AnimationManager *animationManager) {
    ApiController::shaderStorage = shaderStorage;
    ApiController::animationManager = animationManager;
}

void ApiController::onAddShader(AsyncWebServerRequest *request, JsonVariant &json) {
    CallResult<void*> storeResult = shaderStorage->storeShader(json["name"].as<String>(), json["shader"].as<String>());
    if (!storeResult.hasError()) {
        animationManager->scheduleReload();
        request->send(200);
    } else {
        request->send(storeResult.getCode(), "text/plain", storeResult.getMessage());
    }
}

void ApiController::onListShaders(AsyncWebServerRequest *request) {
    CallResult<std::vector<String>*> listResult = shaderStorage->listShaders();
    if (listResult.hasError()) {
        request->send(listResult.getCode(), "text/plain", listResult.getMessage());
        return;
    }
    std::vector<String>* list = listResult.getValue();
    uint16_t size = 100 + list->size() * 50;
    DynamicJsonDocument json(size);
    JsonArray names = json.createNestedArray("shader");
    for(String str : *list) {
        names.add(str);
    }
    delete list;
    
    String response;
    serializeJson(json, response);    
    request->send(200, "application/json", response);
}

void ApiController::onGetShader(String& shader, AsyncWebServerRequest *request) {
    CallResult<String> result = shaderStorage->getShader(shader);
    if (result.hasError()) {
        request->send(result.getCode(), "text/plain", result.getMessage());
        return;
    } 
    request->send(200, "application/json", "\"" + result.getValue() + "\"");
}

void ApiController::onDeleteShader(String& shader, AsyncWebServerRequest *request) {
    if (shaderStorage->deleteShader(shader)) {
        animationManager->scheduleReload();
        request->send(200);
        return;
    } else {
        request->send(404);
        return;
    }
}

void ApiController::onShow(String& shader, AsyncWebServerRequest *request) {
    CallResult<void*> result = animationManager->select(shader);
    if (result.hasError()) {
        request->send(result.getCode(), "text/plain", result.getMessage());
        return;
    }
    request->send(200);
}
