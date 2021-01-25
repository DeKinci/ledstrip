#include "ShaderStorage.h"
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>

#define SD_CS 5

ShaderStorage::ShaderStorage() {
    if(!SD.begin(SD_CS)) {
    Serial.println("Card Mount Failed");
    return;
    }
    uint8_t cardType = SD.cardType();
    if(cardType == CARD_NONE) {
        Serial.println("No SD card attached");
        return;
    }
    if (!SD.exists(shaderDirectory)) {
        Serial.println("Shader dir did not exist, creating");
        if (!SD.mkdir(shaderDirectory)) {
            Serial.println("Can not create shader dir");
        }
    }
    if (!SD.exists(propertiesDirectory)) {
        Serial.println("Properties dir did not exist, creating");
        if (!SD.mkdir(propertiesDirectory)) {
            Serial.println("Can not create properties dir");
        }
    }
}

ShaderStorage::~ShaderStorage() {
    SD.end();
}

CallResult<void*> ShaderStorage::storeShader(const String& name, const String& code) {
    CallResult<void*> result = writeFile(shaderFolderFile(name), code);

    if (result.hasError()) {
        return result;
    }
    if (listener != nullptr) {
        listener->animationAdded(name);
    }
    return result;
}

bool ShaderStorage::hasShader(const String& name) const {
    return SD.exists(shaderFolderFile(name));
}

CallResult<String> ShaderStorage::getShader(const String& name) const {
    return readFile(shaderFolderFile(name));
}

bool ShaderStorage::deleteShader(const String& name) {
    bool result = SD.remove(shaderFolderFile(name));
    if (listener != nullptr && result) {
        listener->animationRemoved(name);
    }
    return result;
}

String ShaderStorage::shaderFolderFile(const String& name) const {
    return shaderDirectory + "/" + name;
}

CallResult<std::vector<String>*> ShaderStorage::listShaders() const {
    File root = SD.open(shaderDirectory);
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

void ShaderStorage::saveLastShader(const String& lastShader) {
    saveProperty("lastShader", lastShader);
}

String ShaderStorage::getLastShader() const{
    return getProperty("lastShader");
}

void ShaderStorage::saveProperty(const String& name, const String& value) {
    if (getProperty(name) != value) {
        writeFile(propertiesDirectory + "/" + name, value);
    }
}

String ShaderStorage::getProperty(const String& name) const{
    CallResult<String> result = readFile(propertiesDirectory + "/" + name);
    if (result.hasError()) {
        return "";
    }
    return result.getValue();
}

CallResult<void*> ShaderStorage::writeFile(const String& name, const String& value) {
    File file = SD.open(name, FILE_WRITE);
 
    if (!file) {
        return CallResult<void*>(nullptr, 500, "error opening file %s for writing", name.c_str());
    }

    if (!file.print(value)) {
        return CallResult<void*>(nullptr, 500, "error writing file %s", name.c_str());
    }

    Serial.printf("wrote to file \n%s\n", value.c_str());

    file.close();
    return CallResult<void*>(nullptr);
}

CallResult<String> ShaderStorage::readFile(const String& name) const {
    File file = SD.open(name, FILE_READ);
 
    if (!file) {
        return CallResult<String>("", 404, "no file %s", name.c_str());
    }

    String result = file.readString();
    return CallResult<String>(result);
}
