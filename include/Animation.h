#ifndef GARLAND_ANIMATION
#define GARLAND_ANIMATION

#include <FastLED.h>

class Animation
{
public:
    virtual void apply(CRGB *leds, size_t size) = 0;
};

#endif //GARLAND_ANIMATION