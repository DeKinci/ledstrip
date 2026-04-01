# microproto-web

Embedded web UI for MicroProto devices. Contains the HTML, JavaScript, and widget rendering code that gets compiled into firmware as compressed static resources.

## Role

microproto-web exists so that:

- Any MicroProto device has a usable control interface out of the box — no separate app install, no cloud dependency
- The UI is self-describing — it reads the property schema (types, constraints, widgets, colors, icons) and renders controls automatically
- The entire UI ships inside the firmware binary as compressed PROGMEM data, served over HTTP

The web UI connects to the device via WebSocket (microproto-ws), speaks the MicroProto binary protocol, and renders controls for every property the device exposes.

## What It Is Not

- Not a framework — it's a concrete, pre-built UI that renders MicroProto properties
- Not a generic web server — webutils handles HTTP; this library just provides the assets
- Not runtime-editable — changes require rebuilding the firmware (or the asset pipeline)

## How It Works

```
Browser → HTTP GET / → webutils serves compressed index.htm from PROGMEM
Browser → WebSocket :81 → microproto-ws handles binary protocol
Browser ← HELLO + SCHEMA → MicroProtoClient.js decodes property definitions
Browser renders controls ← MicroProtoUI.renderAll() maps types to widgets
User interacts → PROPERTY_UPDATE sent over WebSocket → device applies
```

## Contents

### Source assets (`rsc/`)

| File | Purpose |
|------|---------|
| `index.htm` | Main device control page — dark theme, responsive, status indicator |
| `proto.htm` | Protocol debug/demo page — shows raw events, all property types |
| `gen/microproto-client.js` | MicroProto WebSocket client — binary protocol, event system, type validation |
| `gen/microproto-ui.js` | Widget renderer — maps property types + UI hints to HTML controls |

### Generated C++ headers (`src/gen/`)

The build pipeline compresses each asset and generates a C++ header with the data as a `PROGMEM` byte array + a `Resource` struct:

```cpp
// Auto-generated — do not edit
#include <Resource.h>
static const uint8_t w_index_htm_data[] PROGMEM = { ... };
static const Resource w_index_htm = { w_index_htm_data, sizeof(w_index_htm_data), "text/html", ResourceEncoding::GZIP };
```

### Widget entry point (`web/iife-entry.ts`)

TypeScript entry point that bundles widget components (Slider, Toggle, CodeEditor, LedCanvas, etc.) into the `MicroProtoUI` global.

## Serving

The application registers these resources with webutils:

```cpp
#include "gen/w_index_htm.h"
#include "gen/w_microproto_client_js.h"

dispatcher.resource("/", w_index_htm);
dispatcher.resource("/js/proto.js", w_microproto_client_js);
```

ETag is automatically set from `BuildInfo::firmwareHash()` — browsers cache until firmware changes.

## Supported Property Widgets

The UI auto-selects widgets based on property type and UI hints:

- **bool** → toggle switch
- **uint8/int/float** → slider (with min/max from constraints) or numeric input
- **array** → inline editors (color picker for RGB arrays)
- **list** → expandable list editor
- **resource** → code editor (for shaders/scripts)
- **stream** → log viewer (for sys/errorLog, sys/logStream)

Widget selection can be overridden via `UIHints().setWidget(...)` on the property definition.
