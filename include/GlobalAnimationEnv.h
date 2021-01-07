#ifndef GLOBAL_ANIMATION_ENV
#define GLOBAL_ANIMATION_ENV

#include <Arduino.h>

class GlobalAnimationEnv
{
public:
    uint32_t timeMillis = 0;
    uint32_t iteration = 0;
};

#endif //GLOBAL_ANIMATION_ENV