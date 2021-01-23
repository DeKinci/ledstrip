#ifndef SELECT_ANIMATION_LISTENER_H
#define SELECT_ANIMATION_LISTENER_H

#include <Arduino.h>

class SelectAnimationListener
{
public:
    virtual void animationSelected(String name) = 0;
};


#endif //SELECT_ANIMATION_LISTENER_H