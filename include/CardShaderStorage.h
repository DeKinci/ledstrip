#ifndef CARD_SHADER_STORAGE_H
#define CARD_SHADER_STORAGE_H

#include <Arduino.h>
#include <vector>

#include "ShaderStorage.h"

class CardShaderStorage : public ShaderStorage {
public:
    CardShaderStorage();
    virtual ~CardShaderStorage();

    bool hasShader(const String& name) const;
    bool deleteShader(const String& name);
    CallResult<std::vector<String>*> listShaders() const;

protected:
    CallResult<void*> writeFile(const String& name, const String& value);
    CallResult<String> readFile(const String& name) const;
};

#endif //CARD_SHADER_STORAGE_H