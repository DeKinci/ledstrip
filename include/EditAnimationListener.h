#ifndef EDIT_ANIMATION_LISTENER_H
#define EDIT_ANIMATION_LISTENER_H

#include <Arduino.h>

class EditAnimationListener
{
public:
    virtual void animationAdded(String name) = 0;
    virtual void animationRemoved(String name) = 0;
};


#endif //EDIT_ANIMATION_LISTENER_H