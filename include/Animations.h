#ifndef GARLAND_ANIMATIONS
#define GARLAND_ANIMATIONS

#include <Arduino.h>
#include "Animation.h"
#include <math.h>
#include "ShaderStorage.h"

class Rainbow : public Animation
{
private:
    byte counter;

public:
    void apply(CRGB *leds, size_t size)
    {
        for (int i = 0; i < size; i++)
        {
            leds[i] = CHSV(counter + i * 2, 255, 255);
        }
        counter++;
    }
};

class SingleLed : public Animation
{
public:
    void apply(CRGB *leds, size_t size)
    {
        for (int i = 0; i < size; i++)
            leds[i] = CRGB(102, 255, 204);
    }
};

class Fading : public Animation
{
private:
    byte counter;
public:
    void apply(CRGB *leds, size_t size)
    {
        byte value = 127 * (cos(counter * PI / 128.0) + 3);
        for (int i = 0; i < size; i++)
            leds[i] = CHSV(value, 255, 255);
        counter++;
    }
};


#endif //GARLAND_ANIMATIONS