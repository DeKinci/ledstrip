#include "ApiController.h"
#include <ArduinoJson.h>
#include <AsyncJson.h>

ApiController::ApiController(AnimationManager *animationManager) {
    ApiController::animationManager = animationManager;
}

void ApiController::onAddShader(AsyncWebServerRequest *request, JsonVariant &json) {
    String name = json["name"].as<String>();
    String shader = json["shader"].as<String>();
    Serial.println("Received shader " + name);
    Serial.println("*** BEGIN SHADER ***");
    Serial.println(shader);
    Serial.println("*** END SHADER ***");
    CallResult<void*> storeResult = ShaderStorage::get().storeShader(name, shader);
    if (!storeResult.hasError()) {
        animationManager->scheduleReload();
        request->send(200);
    } else {
        request->send(storeResult.getCode(), "text/plain", storeResult.getMessage());
    }
}

void ApiController::onListShaders(AsyncWebServerRequest *request) {
    CallResult<std::vector<String>*> listResult = ShaderStorage::get().listShaders();
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
    CallResult<String> result = ShaderStorage::get().getShader(shader);
    if (result.hasError()) {
        request->send(result.getCode(), "text/plain", result.getMessage());
        return;
    }

    uint16_t size = 200 + result.getValue().length();
    DynamicJsonDocument json(size);
    json["shader"] = result.getValue();

    String response;
    serializeJson(json, response);
    request->send(200, "application/json", response);
}

void ApiController::onDeleteShader(String& shader, AsyncWebServerRequest *request) {
    if (ShaderStorage::get().deleteShader(shader)) {
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

void ApiController::onGetShow(AsyncWebServerRequest *request) {
    String result = animationManager->getCurrent();
    uint16_t size = 200 + result.length();
    DynamicJsonDocument json(size);
    json["name"] = result;
    json["ledLimit"] = animationManager->getCurrentLeds();

    String response;
    serializeJson(json, response);
    request->send(200, "application/json", response);
}
