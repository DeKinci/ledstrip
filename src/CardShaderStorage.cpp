#include "CardShaderStorage.h"

#include <FS.h>
#include <SD.h>
#include <SPI.h>

#define SD_CS 5

CardShaderStorage::CardShaderStorage() : ShaderStorage() {
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

CardShaderStorage::~CardShaderStorage() {
    SD.end();
}

bool CardShaderStorage::hasShader(const String& name) const {
    return SD.exists(shaderFolderFile(name));
}

bool CardShaderStorage::deleteShader(const String& name) {
    bool result = SD.remove(shaderFolderFile(name));
    if (listener != nullptr && result) {
        listener->animationRemoved(name);
    }
    return result;
}

CallResult<std::vector<String>*> CardShaderStorage::listShaders() const {
    File root = SD.open(shaderDirectory);
    File file = root.openNextFile();

    std::vector<String>* result = new std::vector<String>();

    while(file) {
        if (!file.isDirectory()) {
            String name(file.name());
            if (name.startsWith(shaderDirectory)) {
                result->push_back(name.substring(4));
            }
        }
        file = root.openNextFile();
    }

    return CallResult<std::vector<String>*>(result, 200);
}

CallResult<void*> CardShaderStorage::writeFile(const String& name, const String& value) {
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

CallResult<String> CardShaderStorage::readFile(const String& name) const {
    File file = SD.open(name, FILE_READ);
 
    if (!file) {
        return CallResult<String>("", 404, "no file %s", name.c_str());
    }

    String result = file.readString();
    return CallResult<String>(result);
}
