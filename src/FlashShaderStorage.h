#ifndef FLASH_SHADER_STORAGE_H
#define FLASH_SHADER_STORAGE_H

#include <Arduino.h>
#include <vector>

#include "ShaderStorage.h"
#include "CallResult.h"

class FlashShaderStorage : public ShaderStorage {
public:
    FlashShaderStorage();
    virtual ~FlashShaderStorage();

    bool hasShader(const String& name) const;
    bool deleteShader(const String& name);
    CallResult<std::vector<String>> listShaders() const;
    void nuke();

protected:
    CallResult<void*> writeFile(const String& name, const String& value);
    CallResult<String> readFile(const String& name) const;
};

#endif //FLASH_SHADER_STORAGE_H