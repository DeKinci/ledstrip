#include "FlashShaderStorage.h"
#include "web/SocketController.h"

#include <SPIFFS.h>
#include <FS.h>

FlashShaderStorage::FlashShaderStorage() : ShaderStorage() {
    if (!SPIFFS.begin(true)) {
        Serial.println("error mounting SPIFFS");
    }
}

FlashShaderStorage::~FlashShaderStorage() {
    SPIFFS.end();
}

bool FlashShaderStorage::hasShader(const String& name) const {
    return SPIFFS.exists(shaderFolderFile(name));
}

void FlashShaderStorage::nuke() {
    SPIFFS.format();
    ESP.restart();
}

bool FlashShaderStorage::deleteShader(const String& name) {
    bool result = SPIFFS.remove(shaderFolderFile(name));
    SocketController::animationRemoved(name);
    return result;
}

CallResult<std::vector<String>> FlashShaderStorage::listShaders() const {
    File root = SPIFFS.open("/");
    File file = root.openNextFile();

    std::vector<String> result = {};

    while(file) {
        Serial.print("list file ");
        Serial.println(file.name());
        if (!file.isDirectory()) {
            String name(file.name());
            if (name.startsWith(shaderDirectory)) {
                result.push_back(name.substring(3));
            }
        }
        file = root.openNextFile();
    }

    return CallResult<std::vector<String>>(result, 200);
}

CallResult<void*> FlashShaderStorage::writeFile(const String& name, const String& value) {
    File file = SPIFFS.open(name, FILE_WRITE);
 
    if (!file) {
        return CallResult<void*>(nullptr, 500, "error opening file %s for writing", name);
    }

    if (!file.print(value)) {
        return CallResult<void*>(nullptr, 500, "error writing file %s", name);
    }

    file.close();
    Serial.print("saved file ");
    Serial.print(name);
    Serial.print(" ");
    Serial.println(file.name());
    return CallResult<void*>(nullptr);
}

CallResult<String> FlashShaderStorage::readFile(const String& name) const {
    File file = SPIFFS.open(name, FILE_READ);
 
    if (!file) {
        return CallResult<String>("", 404, "no file %s", name.c_str());
    }

    String result = file.readString();
    return CallResult<String>(result);
}
