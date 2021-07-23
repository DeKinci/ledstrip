#ifndef SHADER_STORAGE_H
#define SHADER_STORAGE_H

#include <Arduino.h>
#include <vector>

#include "CallResult.h"
#include "EditAnimationListener.h"

class ShaderStorage {
public:
    
    virtual bool hasShader(const String& name) const = 0;
    virtual bool deleteShader(const String& name) = 0;
    virtual CallResult<std::vector<String>*> listShaders() const = 0;

    virtual CallResult<String> getShader(const String& name) const;
    virtual CallResult<void*> storeShader(const String& name, const String& code);
    virtual void setListener(EditAnimationListener *listener);
    virtual void saveLastShader(const String& lastShader);
    virtual String getLastShader() const;

protected:
    virtual CallResult<void*> writeFile(const String& name, const String& value) = 0;
    virtual CallResult<String> readFile(const String& name) const = 0;

    virtual void saveProperty(const String& name, const String& value);
    virtual String getProperty(const String& name) const;
    virtual String shaderFolderFile(const String& name) const;

    const String shaderDirectory = "/sh";
    const String propertiesDirectory = "/props";

    EditAnimationListener *listener;

private:
    

    
};

#endif //SHADER_STORAGE_H