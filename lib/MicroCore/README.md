# MicroCore

Foundation primitives for embedded C++. The types here replace standard library constructs that are too expensive for constrained devices — `std::function` allocates on the heap, `std::string_view` isn't available on all Arduino toolchains, and there's no portable way to get firmware identity.

## Role

MicroCore exists so that every other library can:

- Use callbacks without heap allocation (`MicroFunction`)
- Pass strings without copying (`StringView`)
- Reference the running firmware build (`BuildInfo`)

MicroCore has **zero dependencies** — no Arduino, no ESP-IDF, no other lib. It's the bottom of the dependency graph. Everything else can depend on it freely.

## What It Is Not

- Not a utility grab-bag — only types that are genuinely needed across multiple libraries belong here
- Not a compatibility layer — it provides embedded-first primitives, not shims over `std::`
- Not growing unless something is needed by 2+ libraries and can't live anywhere else

## MicroFunction

Heap-free callable wrapper. Compile-time enforced size limit — if your lambda captures too much, it won't compile.

```cpp
#include <MicroFunction.h>
using namespace microcore;

// Zero captures: compiles to a raw function pointer (4 bytes)
MicroFunction<void(int), 0> fn = [](int x) { printf("%d", x); };

// Small captures: inline storage, no heap (4 + MaxSize + 4 bytes)
int* ptr = &value;
MicroFunction<void(int), 8> fn = [ptr](int x) { *ptr = x; };
```

Constraints enforced at compile time:
- Callable must be **trivially copyable** (no `std::string`, no `std::shared_ptr`)
- Callable must be **trivially destructible**
- Callable must **fit in MaxSize** bytes

This means `MicroFunction` is just `memcpy` — no constructors, no destructors, no vtables. Safe to put in arrays, structs, static storage.

### Aliases

```cpp
FnPtr<void(int)>   // MaxSize=0, function pointer only
Fn<void(int)>      // MaxSize=sizeof(void*), one pointer capture
Fn16<void(int)>    // MaxSize=16, two pointer captures
```

## StringView

Zero-copy string reference. Points into existing memory — no allocation until you explicitly call `toString()`.

```cpp
#include <StringView.h>

StringView path = "/api/user/42";

path.startsWith("/api");          // true
path.substr(5);                   // "user/42"
path.find('/');                   // 0
path.contains("user");           // true
path.equalsIgnoreCase("API");    // false (different length)

// Implicit conversion from String and const char*
StringView sv = someArduinoString;
StringView sv = "literal";

// Only allocates when you need an owned String
String owned = sv.toString();
```

Used throughout webutils for zero-copy HTTP parsing — request method, path, headers, body are all `StringView` references into the request buffer.

## BuildInfo

Firmware identity for cache invalidation, logging, and diagnostics. Uses ESP-IDF's app descriptor — no extra build steps.

```cpp
#include <BuildInfo.h>

BuildInfo::firmwareHash();   // "a3f7c012" — last 8 hex of ELF SHA256
BuildInfo::buildDate();      // "Mar 30 2026 14:23:01"
BuildInfo::projectName();    // from ESP-IDF app descriptor
```

webutils uses `firmwareHash()` as the default ETag for static resources — clients automatically get `304 Not Modified` until firmware changes.

On native builds (tests), all functions return safe stubs.
