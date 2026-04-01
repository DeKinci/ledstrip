# microinput

Input processing library. Takes raw signals from any source — buttons, encoders, microphones, touch surfaces — and produces meaningful, hardware-agnostic events that the application can act on.

## Role

microinput exists so that:

- Input logic is decoupled from hardware — the same gesture detector works whether the button is a GPIO pin, a BLE remote, or a touch region
- Input processing is reusable across devices — a new board with different inputs uses the same detectors with different wiring
- Complex input patterns (double-click, click-then-hold, audio reactivity) are handled once, correctly, and shared

The library processes input, it doesn't read it. Callers are responsible for debouncing and feeding raw events. microinput turns those into high-level actions.

## What's Here Now

### Gesture Detection

Two composable layers for button-like inputs:

**GestureDetector** — converts press/release timing into basic gestures:

| Gesture | Trigger |
|---------|---------|
| `CLICK` | Press + release within `clickMaxMs` |
| `LONG_CLICK` | Press + release between `clickMaxMs` and `holdThresholdMs` |
| `HOLD_START` | Press held past `holdThresholdMs` |
| `HOLD_TICK` | Repeating at `holdTickMs` while held |
| `HOLD_END` | Released after holding |

**SequenceDetector** — builds multi-gesture patterns on top:

| Action | Trigger |
|--------|---------|
| `SINGLE_CLICK` | One click, confirmed after double-click window expires |
| `DOUBLE_CLICK` | Two clicks within `doubleClickWindowMs` |
| `HOLD_TICK` | Holding from first press |
| `CLICK_HOLD_TICK` | Click, then press-and-hold (e.g. ramp opposite direction) |
| `HOLD_END` | Any hold released |

```cpp
GestureDetector gesture;
SequenceDetector sequence;

gesture.setConfig({ .clickMaxMs = 200, .holdThresholdMs = 400, .holdTickMs = 50 });
sequence.setConfig({ .doubleClickWindowMs = 300 });

gesture.setCallback([&](BasicGesture g) { sequence.onGesture(g); });
sequence.setCallback([](SequenceDetector::Action a) { handleAction(a); });

// Feed from any source
gesture.onPress();
gesture.onRelease();

// In loop
gesture.loop();
sequence.loop();
```

## What Should Grow Here

Input processing that's currently device-specific but belongs in the library as it matures:

- **Rotary encoder** — step counting, acceleration curves, direction detection. Currently in device code with hardware-specific ISR/PCNT wiring.
- **Audio reactivity** — RMS energy, beat detection, frequency bands. Currently in device code as `MicInput` with I2S-specific setup.
- **Touch/slider** — position tracking, swipe detection, tap-vs-drag.

The pattern is the same each time: hardware-specific code reads the raw signal, microinput processes it into actions the app understands.

## Dependencies

- MicroCore (`MicroFunction` for callbacks)
