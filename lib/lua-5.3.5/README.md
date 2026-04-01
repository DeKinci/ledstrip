# lua-5.3.5

Lua 5.3.5 ported for ESP32. Provides a scripting runtime for user-defined LED animations without reflashing firmware.

## Role

Lua exists so that:

- Users can write and upload animation scripts at runtime — no compile/flash cycle
- Animation logic runs in a sandboxed, memory-bounded environment (50KB pool per state)
- Coroutines map naturally to animation frames — `yield()` between frames, resume next tick
- The core language is unmodified stock Lua — any Lua 5.3 knowledge applies directly

## ESP32 Adaptations

The Lua source is stock 5.3.5 with **no code changes**. All ESP32 adaptation is done externally:

### 32-bit mode (`-DLUA_32BITS`)

Set in `platformio.ini`. Switches Lua from 64-bit integers/doubles to 32-bit int/float:

| Type | Stock Lua | ESP32 |
|------|-----------|-------|
| Integer | 64-bit `long long` | 32-bit `int` |
| Float | 64-bit `double` | 32-bit `float` |

Halves the memory per Lua value. Single-precision float is sufficient for color math and animation timing.

### Pool allocator (application layer)

Each Lua state gets a private 50KB heap via ESP-IDF's `multi_heap` API. This:

- Bounds memory usage per script — a runaway script can't starve the system
- Isolates Lua allocations from the rest of the firmware
- Falls back to standard malloc if the pool is exhausted

The allocator is injected via `lua_newstate(luaPoolAlloc, heap)` — Lua's standard custom-allocator hook. No changes to Lua source needed.

### What works

- Full Lua 5.3 language: closures, metatables, coroutines, iterators
- All standard libraries: string, table, math, coroutine, io, os, debug
- Coroutine-based animation loop (`coroutine.yield()` between frames)

### What doesn't apply

- `package` library exists but dynamic `.so`/`.dll` loading has no effect in firmware
- `os.execute()` / subprocess pipes — no OS process model
- File I/O (`io.*`) depends on whether SPIFFS/LittleFS is mounted

## Animation Integration

The application layer (outside this library) provides LED-specific bindings:

- Color constructors (RGB, HSV) packed as 32-bit `lua_Integer`
- Segment userdata for direct LED array access
- Blend/interpolation functions bridging to FastLED
- Frame coroutine: script yields per frame, runtime resumes on next tick

## Building

No special build steps. PlatformIO compiles all `.c` files in `src/`. The only required flag:

```ini
build_flags = -DLUA_32BITS
```
