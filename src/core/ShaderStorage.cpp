#include "ShaderStorage.h"
#include "FlashShaderStorage.h"

ShaderStorage* ShaderStorage::aStorage = nullptr;

void ShaderStorage::init() {
    static ShaderStorage* fss = new FlashShaderStorage();
    aStorage = fss;
}

ShaderStorage& ShaderStorage::get() {
    return *aStorage;
}

CallResult<String> ShaderStorage::getShader(const String& name) const {
    return readFile(shaderFolderFile(name));
}

CallResult<void*> ShaderStorage::storeShader(const String& name, const String& code) {
    CallResult<void*> result = writeFile(shaderFolderFile(name), code);
    return result;
}

String ShaderStorage::shaderFolderFile(const String& name) const {
    return "/" + shaderDirectory + "&" + name;
}

void ShaderStorage::saveLastShader(const String& lastShader) {
    saveProperty("lastShader", lastShader);
}

String ShaderStorage::getLastShader() const{
    return getProperty("lastShader");
}

void ShaderStorage::saveProperty(const String& name, const String& value) {
    if (getProperty(name) != value) {
        writeFile(propertiesDirectory + "&" + name, value);
    }
}

String ShaderStorage::getProperty(const String& name, const String& aDefault) const{
    CallResult<String> result = readFile(propertiesDirectory + "&" + name);
    if (result.hasError()) {
        return aDefault;
    }
    return result.getValue();
}
