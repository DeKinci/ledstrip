# Ledstrip
Controlling addressable led strip with esp32 and lua shaders
This project is as is, made with lowest effort possible and is not ment for public use

Use platformio/VSCode for development

# Setting

*main.cpp*

NUM_LEDS

LEFT_BTN_PIN

RIGHT_BTN_PIN

BTN_DELAY - delay to register click, not swipe

LED_PIN - led strip control pin

*AnimationManager.h*

CACHE_SIZE - how many last animations (and heavy lua states) store in memory

*ShaderStorage.cpp*

SD_CS - SD card CS pin, others are default

# Usage
To create your own powerful animations provide shaders in lua script form, e.g. white light:
```
function color(position) 
    return {255, 0, 255} 
end
```
function name must be `color`, and return is a tuple of three HSV values for led at `position`

Led strip provides easy wifi connection, openAPI interface as well as websockets for low latency usage
