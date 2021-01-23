#include "ShaderStorage.h"
#include <Arduino.h>
#include <SPIFFS.h>

ShaderStorage::ShaderStorage() {
    if (!SPIFFS.begin(true)) {
        Serial.println("error mounting SPIFFS");
    }
}

ShaderStorage::~ShaderStorage() {
    SPIFFS.end();
}

CallResult<void*> ShaderStorage::storeShader(const String& name, const String& code) {
    File file = SPIFFS.open(shaderFolderFile(name), FILE_WRITE);
 
    if (!file) {
        return CallResult<void*>(nullptr, 500, "error opening file %s for writing", name);
    }

    if (!file.print(code)) {
        return CallResult<void*>(nullptr, 500, "error writing file %s", name);
    }

    file.close();
    if (listener != nullptr) {
        listener->animationAdded(name);
    }
    return CallResult<void*>(nullptr);
}

bool ShaderStorage::hasShader(const String& name) const {
    return SPIFFS.exists(shaderFolderFile(name));
}

CallResult<String> ShaderStorage::getShader(const String& name) const {
    File file = SPIFFS.open(shaderFolderFile(name), FILE_READ);
 
    if (!file) {
        return CallResult<String>("", 500, "error opening file %s for reading", name);
    }

    return file.readString();
}

bool ShaderStorage::deleteShader(const String& name) {
    bool result = SPIFFS.remove(shaderFolderFile(name));
    if (listener != nullptr && result) {
        listener->animationRemoved(name);
    }
    return result;
}

String ShaderStorage::shaderFolderFile(const String& name) const {
    return "/sh/" + name;
}

CallResult<std::vector<String>*> ShaderStorage::listShaders() const {
    File root = SPIFFS.open("/");
    File file = root.openNextFile();

    std::vector<String>* result = new std::vector<String>();

    while(file) {
        if (!file.isDirectory()) {
            String name(file.name());
            if (name.startsWith("/sh/")) {
                result->push_back(name.substring(4));
            }
        }
        file = root.openNextFile();
    }

    return CallResult<std::vector<String>*>(result, 200);
}

void ShaderStorage::setListener(EditAnimationListener *listener) {
    ShaderStorage::listener = listener;
}
