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

    JsonDocument json;
    JsonArray names = json["shader"].to<JsonArray>();
    for (String str : list) {
        names.add(str);
    }

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    serializeJson(json, *response);
    request->send(response);
}

void onGetShader(String &shader, AsyncWebServerRequest *request) {
    CallResult<String> result = ShaderStorage::get().getShader(shader);
    if (result.hasError()) {
        request->send(result.getCode(), "text/plain", result.getMessage());
        return;
    }

    JsonDocument json;
    json["shader"] = result.getValue();

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    serializeJson(json, *response);
    request->send(response);
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
    JsonDocument json;
    json["name"] = Anime::getCurrent();
    json["ledLimit"] = Anime::getCurrentLeds();

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    serializeJson(json, *response);
    request->send(response);
}

}  // namespace ApiController