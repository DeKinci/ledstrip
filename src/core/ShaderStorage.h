#ifndef SHADER_STORAGE_H
#define SHADER_STORAGE_H

#include <Arduino.h>
#include <vector>

#include "CallResult.h"

class ShaderStorage {
public:
    static void init();
    static ShaderStorage& get();
    
    virtual bool hasShader(const String& name) const = 0;
    virtual bool deleteShader(const String& name) = 0;
    virtual CallResult<std::vector<String>> listShaders() const = 0;
    virtual void nuke() = 0;

    virtual CallResult<String> getShader(const String& name) const;
    virtual CallResult<void*> storeShader(const String& name, const String& code);
    virtual void saveLastShader(const String& lastShader);
    virtual String getLastShader() const;
    virtual void saveProperty(const String& name, const String& value);
    virtual String getProperty(const String& name, const String& aDefault = "") const;

protected:
    virtual CallResult<void*> writeFile(const String& name, const String& value) = 0;
    virtual CallResult<String> readFile(const String& name) const = 0;

    virtual String shaderFolderFile(const String& name) const;

    const String shaderDirectory = "sh";
    const String propertiesDirectory = "/props";

private:
    static ShaderStorage* aStorage;
};

#endif //SHADER_STORAGE_H