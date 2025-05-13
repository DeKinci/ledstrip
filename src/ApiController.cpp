#include "ApiController.h"

#include <ArduinoJson.h>
#include <AsyncJson.h>

namespace ApiController {

void onAddShader(AsyncWebServerRequest *request, JsonVariant &json) {
    String name = json["name"].as<String>();
    String shader = json["shader"].as<String>();
    Serial.println("Received shader " + name);
    Serial.println("*** BEGIN SHADER ***");
    Serial.println(shader);
    Serial.println("*** END SHADER ***");
    CallResult<void *> storeResult = ShaderStorage::get().storeShader(name, shader);
    if (!storeResult.hasError()) {
        Anime::scheduleReload();
        request->send(200);
    } else {
        request->send(storeResult.getCode(), "text/plain", storeResult.getMessage());
    }
}

void onListShaders(AsyncWebServerRequest *request) {
    CallResult<std::vector<String>> listResult = ShaderStorage::get().listShaders();
    if (listResult.hasError()) {
        request->send(listResult.getCode(), "text/plain", listResult.getMessage());
        return;
    }
    std::vector<String> list = listResult.getValue();
    uint16_t size = 100 + list.size() * 50;
    DynamicJsonDocument json(size);
    JsonArray names = json.createNestedArray("shader");
    for (String str : list) {
        names.add(str);
    }

    String response;
    serializeJson(json, response);
    request->send(200, "application/json", response);
}

void onGetShader(String &shader, AsyncWebServerRequest *request) {
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

void onDeleteShader(String &shader, AsyncWebServerRequest *request) {
    if (ShaderStorage::get().deleteShader(shader)) {
        Anime::scheduleReload();
        request->send(200);
        return;
    } else {
        request->send(404);
        return;
    }
}

void onShow(String &shader, AsyncWebServerRequest *request) {
    CallResult<void *> result = Anime::select(shader);
    if (result.hasError()) {
        request->send(result.getCode(), "text/plain", result.getMessage());
        return;
    }
    request->send(200);
}

void onGetShow(AsyncWebServerRequest *request) {
    String result = Anime::getCurrent();
    uint16_t size = 200 + result.length();
    DynamicJsonDocument json(size);
    json["name"] = result;
    json["ledLimit"] = Anime::getCurrentLeds();

    String response;
    serializeJson(json, response);
    request->send(200, "application/json", response);
}

}  // namespace ApiController