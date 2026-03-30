#ifndef MIC_INPUT_HPP
#define MIC_INPUT_HPP

#ifdef MIC_ENABLED

#include <Arduino.h>

namespace MicInput {
    void init();
    void loop();
    float getVolume();  // 0.0 .. 1.0

    // Debug audio stream — raw 16-bit PCM over WebSocket on port 82
    void initDebugStream();
    void loopDebugStream();
}

#endif // MIC_ENABLED
#endif // MIC_INPUT_HPP
