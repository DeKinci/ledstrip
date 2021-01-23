#ifndef SHADER_STORAGE_H
#define SHADER_STORAGE_H

#include <Arduino.h>
#include <vector>

#include "CallResult.h"
#include "EditAnimationListener.h"

class ShaderStorage {
public:
    ShaderStorage();
    virtual ~ShaderStorage();
    CallResult<void*> storeShader(const String& name, const String& code);
    bool hasShader(const String& name) const;
    CallResult<String> getShader(const String& name) const;
    bool deleteShader(const String& name);
    CallResult<std::vector<String>*> listShaders() const;

    void setListener(EditAnimationListener *listener);

private:
    bool begin();
    String shaderFolderFile(const String& name) const;
    EditAnimationListener *listener;
};

#endif //SHADER_STORAGE_H