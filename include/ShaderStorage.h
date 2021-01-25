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

    void saveLastShader(const String& lastShader);
    String getLastShader() const;

private:
    bool begin();
    String shaderFolderFile(const String& name) const;

    void saveProperty(const String& name, const String& value);
    String getProperty(const String& name) const;

    CallResult<void*> writeFile(const String& name, const String& value);
    CallResult<String> readFile(const String& name) const;

    EditAnimationListener *listener;
    const String shaderDirectory = "/sh";
    const String propertiesDirectory = "/props";
};

#endif //SHADER_STORAGE_H