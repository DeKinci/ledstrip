# Libraries

## Philosophy

Every library is a small, isolated module with a single responsibility. Libraries compose — they don't inherit, don't reach into each other's internals, and don't assume what else is running. A library should work if you drop it into a different project with different neighbors.

**Principles:**

- **Zero-dep where possible.** Foundation libraries (MicroCore, microlog, microinput) depend on nothing. This keeps them universally usable.
- **No heap allocation in hot paths.** Fixed-size arrays, `MicroFunction` instead of `std::function`, `StringView` instead of `String`. Heap allocation is reserved for one-time setup.
- **Non-blocking always.** Nothing blocks in `loop()`. Every library uses state machines that do a unit of work and return.
- **Compose at the edges.** Libraries don't know about each other. Integration libraries (microlog-up, microproto-ws, microproto-ble) exist specifically to bridge two systems. The application wires everything together.
- **Shared infrastructure, not shared state.** `HttpDispatcher` is a shared route table — libraries register routes on it without owning it. `MicroBLE::init()` is a shared BLE stack — multiple GATT services coexist. The application owns the lifecycle.

## Dependency Graph

```
                         ┌─────────────────────────────────┐
                         │          Application             │
                         └──┬──────┬──────┬──────┬─────────┘
                            │      │      │      │
              ┌─────────────┘      │      │      └──────────────┐
              ▼                    ▼      ▼                     ▼
     ┌─────────────┐    ┌────────────┐  ┌──────────┐   ┌──────────────┐
     │ microproto- │    │ microproto-│  │microlog- │   │MicroProto-   │
     │ web         │    │ ws         │  │up        │   │Matter        │
     │ (assets)    │    │ (WS txp)   │  │(log→prop)│   │(Matter↔prop) │
     └─────────────┘    └─────┬──────┘  └──┬───┬───┘   └──────┬───────┘
                              │            │   │               │
                  ┌───────────┤        ┌───┘   └───┐    ┌──────┘
                  │           │        │           │    │
                  ▼           ▼        ▼           ▼    ▼
           ┌──────────┐  ┌────────────────┐  ┌───────────┐
           │links2004/ │  │  microproto    │  │ microlog  │
           │WebSockets │  │  (protocol)    │  │ (logging) │
           └─────┬────┘  └───────┬────────┘  └───────────┘
                 │               │
                 │               ▼
                 │        ┌──────────┐
                 │        │MicroCore │
                 │        │(foundation)
                 │        └──────────┘
                 │
                 │   ┌──────────────┐
                 └──►│ microproto-  │
                     │ gw (gateway) ├──► microproto
                     └──────────────┘

     ┌──────────────┐    ┌──────────┐    ┌──────────────┐
     │ microproto-  │    │ microble │    │   bleman     │
     │ ble (BLE txp)├───►│ (GATT)   │    │ (BLE central)│
     └──────┬───────┘    └──────────┘    └──────────────┘
            │
            ▼
       microproto

     ┌──────────┐    ┌──────────┐    ┌──────────┐
     │ webutils │    │ wifiman  │    │microinput│
     │ (HTTP)   │◄───┤ (WiFi)   │    │ (input)  │
     └────┬─────┘    └──────────┘    └──────────┘
          │
          ▼
     MicroCore, microlog

     ┌──────────┐
     │lua-5.3.5 │    (standalone, no deps)
     └──────────┘
```

## Libraries by Domain

### Protocol

The core of the system. A binary property-based protocol where the device declares typed properties with metadata, clients connect and receive the schema, and bidirectional updates flow automatically.

| Library | Role |
|---------|------|
| [microproto](microproto/) | Protocol engine — properties, schema, dirty tracking, RPC, transport abstraction |
| [microproto-ws](microproto-ws/) | WebSocket transport adapter |
| [microproto-ble](microproto-ble/) | BLE transport adapter (uses microble for GATT) |
| [microproto-gw](microproto-gw/) | Gateway client transport — outbound WebSocket to remote gateway |
| [microproto-web](microproto-web/) | Embedded web UI — auto-renders controls from property schema |
| [MicroProtoMatter](MicroProtoMatter/) | Matter protocol bridge — maps properties to Matter clusters |

### BLE

Two distinct roles on the same NimBLE stack: peripheral (exposing services) and central (connecting to other devices).

| Library | Role |
|---------|------|
| [microble](microble/) | Peripheral — shared GATT server, service registration, connection slots, message fragmentation |
| [bleman](bleman/) | Central — scan for, connect to, and drive external BLE peripherals via pluggable drivers |
| [microproto-ble](microproto-ble/) | Bridges microproto onto microble's GATT infrastructure |

### Networking

| Library | Role |
|---------|------|
| [webutils](webutils/) | HTTP toolkit — zero-copy parser, route dispatcher, response builder, static resource serving |
| [wifiman](wifiman/) | WiFi lifecycle — multi-network, captive portal, credential persistence, non-blocking state machine |

### Logging & Observability

| Library | Role |
|---------|------|
| [microlog](microlog/) | Structured logging + metrics (Gauge, Counter). Zero deps, usable everywhere |
| [microlog-up](microlog-up/) | Bridges microlog into microproto — logs become streamable properties |

### Input

| Library | Role |
|---------|------|
| [microinput](microinput/) | Hardware-agnostic input processing — gesture detection, sequences, future: encoder, audio reactivity |

### Scripting

| Library | Role |
|---------|------|
| [lua-5.3.5](lua-5.3.5/) | Embedded Lua runtime — stock 5.3.5 with 32-bit mode for ESP32. Powers user-defined animations |

### Foundation

| Library | Role |
|---------|------|
| [MicroCore](MicroCore/) | Primitives — `MicroFunction` (heap-free callbacks), `StringView` (zero-copy strings), `BuildInfo` (firmware identity) |

## Library Index

| Library | Description | Deps |
|---------|-------------|------|
| [MicroCore](MicroCore/) | Foundation primitives for embedded C++ | — |
| [microlog](microlog/) | Structured logging and metrics | — |
| [microinput](microinput/) | Input processing and gesture detection | MicroCore |
| [microble](microble/) | BLE GATT server infrastructure (NimBLE) | — |
| [webutils](webutils/) | HTTP server, parsing, route dispatch | MicroCore, microlog |
| [wifiman](wifiman/) | WiFi lifecycle and captive portal | webutils, ArduinoJson |
| [bleman](bleman/) | BLE peripheral manager with driver system | ArduinoJson |
| [microproto](microproto/) | Binary property-based protocol engine | MicroCore |
| [microproto-ws](microproto-ws/) | MicroProto over WebSocket | microproto, WebSockets |
| [microproto-ble](microproto-ble/) | MicroProto over BLE GATT | microproto, microble |
| [microproto-gw](microproto-gw/) | MicroProto gateway client transport | microproto, WebSockets |
| [microproto-web](microproto-web/) | Embedded web UI for MicroProto devices | — |
| [microlog-up](microlog-up/) | Log streaming via MicroProto properties | microlog, microproto |
| [MicroProtoMatter](MicroProtoMatter/) | Matter protocol bridge | microproto, microlog |
| [lua-5.3.5](lua-5.3.5/) | Lua 5.3.5 runtime (32-bit mode for ESP32) | — |
