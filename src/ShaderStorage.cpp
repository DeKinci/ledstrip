#include "ShaderStorage.h"

void ShaderStorage::setListener(EditAnimationListener *listener) {
    ShaderStorage::listener = listener;
}

CallResult<String> ShaderStorage::getShader(const String& name) const {
    return readFile(shaderFolderFile(name));
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

String ShaderStorage::shaderFolderFile(const String& name) const {
    return shaderDirectory + "/" + name;
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
