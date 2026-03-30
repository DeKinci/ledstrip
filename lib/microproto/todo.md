# Not MVP (Deferred Features)

## Wire Protocol
- Delta updates with keyframes (opcode 0x2 reserved)
- Reliable/unreliable update mode — use delta or not

## Schema
- Pattern (regex) validation for strings

## Distributed/Mesh
- GROUP/GLOBAL property levels with versioning
- Version-based conflict resolution (last-write-wins)
- source_node_id tracking for multi-node updates
- ESP-NOW transport
- BLE mesh support

## Performance
- Backpressure handling
- Rate limiting

## UI
- Show/hide properties and resources dynamically
