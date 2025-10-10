#include "ApiController.h"

#include <ArduinoJson.h>
#include <AsyncJson.h>

namespace ApiController {

void onAddShader(AsyncWebServerRequest *request, JsonVariant &json) {
    uint32_t start = millis();
    String name = json["name"].as<String>();
    String shader = json["shader"].as<String>();
    Serial.printf("[API] onAddShader start: %s (%d bytes)\n", name.c_str(), shader.length());

    uint32_t beforeStore = millis();
    CallResult<void *> storeResult = ShaderStorage::get().storeShader(name, shader);
    uint32_t afterStore = millis();
    Serial.printf("[API] storeShader took %dms\n", afterStore - beforeStore);

    if (!storeResult.hasError()) {
        Anime::scheduleReload();
        request->send(200);
    } else {
        request->send(storeResult.getCode(), "text/plain", storeResult.getMessage());
    }
    Serial.printf("[API] onAddShader total: %dms\n", millis() - start);
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