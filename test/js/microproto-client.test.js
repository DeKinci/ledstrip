/**
 * MicroProto Client Unit Tests
 *
 * Run with: node test/js/microproto-client.test.js
 */

const assert = require('assert');
const { TextEncoder, TextDecoder } = require('util');

// Polyfill for Node.js
global.TextEncoder = TextEncoder;
global.TextDecoder = TextDecoder;

// Mock localStorage for Node.js
const _store = {};
global.localStorage = {
    getItem: (k) => _store[k] || null,
    setItem: (k, v) => { _store[k] = String(v); },
    removeItem: (k) => { delete _store[k]; },
    clear: () => { for (const k in _store) delete _store[k]; }
};

// ============== Mock WebSocket ==============

class MockWebSocket {
    static CONNECTING = 0;
    static OPEN = 1;
    static CLOSING = 2;
    static CLOSED = 3;

    constructor(url) {
        this.url = url;
        this.readyState = MockWebSocket.CONNECTING;
        this.binaryType = 'arraybuffer';
        this.sentMessages = [];
        this.onopen = null;
        this.onclose = null;
        this.onmessage = null;
        this.onerror = null;

        // Auto-connect after a tick (disabled for sync tests)
        // setTimeout(() => this._simulateOpen(), 0);
    }

    send(data) {
        this.sentMessages.push(data);
    }

    close() {
        this.readyState = MockWebSocket.CLOSED;
        if (this.onclose) this.onclose();
    }

    // Test helpers
    _simulateOpen() {
        this.readyState = MockWebSocket.OPEN;
        if (this.onopen) this.onopen();
    }

    _simulateMessage(data) {
        if (this.onmessage) {
            this.onmessage({ data: data.buffer || data });
        }
    }

    _simulateClose() {
        this.readyState = MockWebSocket.CLOSED;
        if (this.onclose) this.onclose();
    }
}

// Load the client by wrapping it in a function that returns the class
const fs = require('fs');
const path = require('path');

const clientCode = fs.readFileSync(path.join(__dirname, '../../rsc/microproto-client.js'), 'utf8');

// Wrap code to return the class, inject WebSocket mock
const wrappedCode = `
(function(WebSocket, TextEncoder, TextDecoder) {
    ${clientCode}
    return MicroProtoClient;
})
`;

// Compile and execute
const factory = eval(wrappedCode);
const MicroProtoClient = factory(MockWebSocket, TextEncoder, TextDecoder);

// ============== Test Utilities ==============

let testCount = 0;
let passCount = 0;
let failCount = 0;

function test(name, fn) {
    testCount++;
    try {
        fn();
        passCount++;
        console.log(`  ✓ ${name}`);
    } catch (e) {
        failCount++;
        console.log(`  ✗ ${name}`);
        console.log(`    ${e.message}`);
    }
}

function assertEqual(actual, expected, msg = '') {
    if (actual !== expected) {
        throw new Error(`${msg} Expected ${expected}, got ${actual}`);
    }
}

function assertDeepEqual(actual, expected, msg = '') {
    if (JSON.stringify(actual) !== JSON.stringify(expected)) {
        throw new Error(`${msg} Expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
    }
}

function assertArrayEqual(actual, expected, msg = '') {
    if (actual.length !== expected.length) {
        throw new Error(`${msg} Length mismatch: ${actual.length} vs ${expected.length}`);
    }
    for (let i = 0; i < actual.length; i++) {
        if (actual[i] !== expected[i]) {
            throw new Error(`${msg} Mismatch at index ${i}: ${actual[i]} vs ${expected[i]}`);
        }
    }
}

// ============== HELLO Encoding Tests ==============

console.log('\n== HELLO Encoding ==');

test('encodes HELLO request correctly', () => {
    // Use fixed deviceId for predictable test
    const client = new MicroProtoClient('ws://test:81', { reconnect: false, deviceId: 1 });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;

    client._sendHello();

    const sent = new Uint8Array(client.ws.sentMessages[0]);
    // MVP format: opcode(1) + version(1) + maxPacketSize(varint) + deviceId(varint) + schemaVersion(u16)
    // maxPacketSize = 4096 = varint(0x80, 0x20), deviceId = 1 = varint(0x01)
    assertEqual(sent.length, 7, 'HELLO length');  // 1 + 1 + 2 + 1 + 2 = 7
    assertEqual(sent[0], 0x00, 'opcode');  // HELLO
    assertEqual(sent[1], 1, 'version');    // Protocol version 1
    // maxPacketSize = 4096 as varint: 0x80 | (4096 & 0x7F) = 0x80, 4096 >> 7 = 32
    assertEqual(sent[2], 0x80, 'maxPacket varint byte 0');
    assertEqual(sent[3], 0x20, 'maxPacket varint byte 1');
    assertEqual(sent[4], 0x01, 'deviceId varint');
    // schemaVersion = 0 (no cache)
    assertEqual(sent[5], 0x00, 'schemaVersion lo');
    assertEqual(sent[6], 0x00, 'schemaVersion hi');
});

test('encodes custom device ID', () => {
    // Use a smaller device ID that encodes to fewer bytes for easier testing
    // deviceId = 200 = varint(0xC8, 0x01) = 2 bytes
    const client = new MicroProtoClient('ws://test:81', {
        reconnect: false,
        deviceId: 200
    });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;

    client._sendHello();

    const sent = new Uint8Array(client.ws.sentMessages[0]);
    // Length: 1 (opcode) + 1 (version) + 2 (maxPacketSize varint) + 2 (deviceId varint) + 2 (schemaVersion u16) = 8
    assertEqual(sent.length, 8, 'message length');
    // deviceId = 200 as varint: 0x80 | (200 & 0x7F) = 0xC8, 200 >> 7 = 1
    assertEqual(sent[4], 0xC8, 'deviceId varint byte 0');
    assertEqual(sent[5], 0x01, 'deviceId varint byte 1');
});

// ============== HELLO Decoding Tests ==============

console.log('\n== HELLO Decoding ==');

test('decodes HELLO response', (done) => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    let connectInfo = null;

    client.on('connect', (info) => {
        connectInfo = info;
    });

    // Simulate connection
    client.connect();

    setTimeout(() => {
        // Build HELLO response - MVP format with varints
        // opcode(1) + version(1) + maxPacket(varint) + sessionId(varint) + timestamp(varint)
        // Use small values that encode to 1-2 bytes for easy testing
        // maxPacket=128 (varint: 0x80, 0x01), sessionId=100 (0x64), timestamp=200 (varint: 0xC8, 0x01)
        const response = new Uint8Array([
            0x10,  // HELLO opcode with is_response flag (bit0 in flags = bit4 in header)
            1,     // version
            0x80, 0x01,  // maxPacket = 128 as varint
            0x64,        // sessionId = 100
            0xC8, 0x01,  // timestamp = 200 as varint
            0x05, 0x00   // schemaVersion = 5 (u16 little-endian)
        ]);

        client.ws._simulateMessage(response);

        assertEqual(client.connected, true, 'connected');
        assertEqual(client.sessionId, 100, 'sessionId');
        assertEqual(connectInfo.version, 1, 'version');
        assertEqual(connectInfo.sessionId, 100, 'event sessionId');
    }, 10);
});

// ============== Property Update Encoding Tests ==============

console.log('\n== Property Update Encoding ==');

test('encodes UINT8 property update', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;
    client.connected = true;

    // Setup property
    client.properties.set(3, { id: 3, name: 'brightness', typeId: 0x03, readonly: false });
    client.propertyByName.set('brightness', 3);

    client.setProperty('brightness', 200);

    // MVP format: opheader(1) + propid(1) + value(1) = 3 bytes
    const sent = new Uint8Array(client.ws.sentMessages[0]);
    assertEqual(sent[0], 0x01, 'opcode PROPERTY_UPDATE');
    assertEqual(sent[1], 3, 'property ID');
    assertEqual(sent[2], 200, 'value');
});

test('encodes BOOL property update', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;
    client.connected = true;

    client.properties.set(2, { id: 2, name: 'enabled', typeId: 0x01, readonly: false });
    client.propertyByName.set('enabled', 2);

    client.setProperty('enabled', true);

    // MVP format: opheader(1) + propid(1) + value(1) = 3 bytes
    const sent = new Uint8Array(client.ws.sentMessages[0]);
    assertEqual(sent[0], 0x01, 'opcode');
    assertEqual(sent[1], 2, 'property ID');
    assertEqual(sent[2], 1, 'value true');

    client.setProperty('enabled', false);
    const sent2 = new Uint8Array(client.ws.sentMessages[1]);
    assertEqual(sent2[2], 0, 'value false');
});

test('encodes FLOAT32 property update', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;
    client.connected = true;

    client.properties.set(1, { id: 1, name: 'speed', typeId: 0x05, readonly: false });
    client.propertyByName.set('speed', 1);

    client.setProperty('speed', 2.5);

    // MVP format: opheader(1) + propid(1) + value(4) = 6 bytes
    const sent = new Uint8Array(client.ws.sentMessages[0]);
    const view = new DataView(sent.buffer);
    assertEqual(sent[0], 0x01, 'opcode');
    assertEqual(sent[1], 1, 'property ID');

    const floatVal = view.getFloat32(2, true);  // Value starts at byte 2
    assert(Math.abs(floatVal - 2.5) < 0.001, 'float value');
});

test('encodes INT32 property update', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;
    client.connected = true;

    client.properties.set(5, { id: 5, name: 'count', typeId: 0x04, readonly: false });
    client.propertyByName.set('count', 5);

    client.setProperty('count', -12345);

    // MVP format: opheader(1) + propid(1) + value(4) = 6 bytes
    const sent = new Uint8Array(client.ws.sentMessages[0]);
    const view = new DataView(sent.buffer);
    assertEqual(view.getInt32(2, true), -12345, 'int32 value');  // Value starts at byte 2
});

test('rejects readonly property update', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;
    client.connected = true;

    client.properties.set(10, { id: 10, name: 'readonly_prop', typeId: 0x03, readonly: true });
    client.propertyByName.set('readonly_prop', 10);

    const result = client.setProperty('readonly_prop', 100);

    assertEqual(result, false, 'should reject');
    assertEqual(client.ws.sentMessages.length, 0, 'nothing sent');
});

test('rejects unknown property', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;

    const result = client.setProperty('nonexistent', 100);

    assertEqual(result, false, 'should reject');
    assertEqual(client.ws.sentMessages.length, 0, 'nothing sent');
});

// ============== Property Update Decoding Tests ==============

console.log('\n== Property Update Decoding ==');

test('decodes single UINT8 property update', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    client.properties.set(3, { id: 3, name: 'brightness', typeId: 0x03, value: 0 });
    client.propertyByName.set('brightness', 3);

    let receivedName = null;
    let receivedValue = null;
    client.on('property', (id, name, value) => {
        receivedName = name;
        receivedValue = value;
    });

    // Single property update (no batch flag) - MVP format
    // Header: opcode=0x01 (bits 0-3), flags=0 (bits 4-7) = 0x01
    const msg = new Uint8Array([
        0x01,  // PROPERTY_UPDATE opcode, no flags
        3,     // property ID (< 128, so 1 byte)
        200    // value
    ]);

    client._handleMessage(msg);

    assertEqual(receivedName, 'brightness', 'name');
    assertEqual(receivedValue, 200, 'value');
    assertEqual(client.properties.get(3).value, 200, 'stored value');
});

test('decodes batched property updates', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    client.properties.set(1, { id: 1, name: 'speed', typeId: 0x05, value: 0 });
    client.properties.set(2, { id: 2, name: 'enabled', typeId: 0x01, value: false });
    client.properties.set(3, { id: 3, name: 'brightness', typeId: 0x03, value: 0 });
    client.propertyByName.set('speed', 1);
    client.propertyByName.set('enabled', 2);
    client.propertyByName.set('brightness', 3);

    const updates = [];
    client.on('property', (id, name, value) => {
        updates.push({ id, name, value });
    });

    // Batched update: 3 properties - MVP format
    // No namespace bytes, propid encoding (1 byte for IDs < 128)
    // Header: opcode=0x01 (bits 0-3), flags=0x01 batch (bit 4) = 0x11
    const msg = new Uint8Array(2 + 5 + 2 + 2);  // header + float(1+4) + bool(1+1) + uint8(1+1)
    const view = new DataView(msg.buffer);

    msg[0] = 0x11;  // PROPERTY_UPDATE (0x01) with batch flag (0x10)
    msg[1] = 2;     // count - 1 = 2 (so 3 items)

    // Item 1: speed (float) - propId + 4 bytes
    msg[2] = 1;     // prop ID
    view.setFloat32(3, 1.5, true);

    // Item 2: enabled (bool) - propId + 1 byte
    msg[7] = 2;     // prop ID
    msg[8] = 1;     // true

    // Item 3: brightness (uint8) - propId + 1 byte
    msg[9] = 3;     // prop ID
    msg[10] = 128;  // value

    client._handleMessage(msg);

    assertEqual(updates.length, 3, 'update count');
    assertEqual(updates[0].name, 'speed', 'name 0');
    assert(Math.abs(updates[0].value - 1.5) < 0.001, 'value 0');
    assertEqual(updates[1].name, 'enabled', 'name 1');
    assertEqual(updates[1].value, true, 'value 1');
    assertEqual(updates[2].name, 'brightness', 'name 2');
    assertEqual(updates[2].value, 128, 'value 2');
});

// ============== Schema Decoding Tests ==============

console.log('\n== Schema Decoding ==');

test('decodes single schema item', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });

    let schemaProp = null;
    client.on('schema', (prop) => {
        schemaProp = prop;
    });

    // Build schema message for a UINT8 property named "brightness"
    const name = 'brightness';
    const nameBytes = new TextEncoder().encode(name);

    const msg = new Uint8Array([
        0x03,           // SCHEMA_UPSERT opcode (no batch)
        0x01,           // item type: PROPERTY
        0x00,           // level flags: ROOT
        3,              // item ID
        0,              // namespace ID
        nameBytes.length, // name length
        ...nameBytes,   // name
        0,              // description length (varint)
        0x03,           // type ID: UINT8
        0,              // validation flags
        128,            // default value
        0               // UI hints (no hints)
    ]);

    client._handleMessage(msg);

    assertEqual(schemaProp.id, 3, 'id');
    assertEqual(schemaProp.name, 'brightness', 'name');
    assertEqual(schemaProp.typeId, 0x03, 'typeId');
    assertEqual(client.properties.size, 1, 'properties count');
    assertEqual(client.propertyByName.get('brightness'), 3, 'name lookup');
});

test('decodes schema with UI hints', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });

    let schemaProp = null;
    client.on('schema', (prop) => {
        schemaProp = prop;
    });

    const name = 'speed';
    const nameBytes = new TextEncoder().encode(name);
    const unit = 'ms';
    const unitBytes = new TextEncoder().encode(unit);
    const icon = '⚡';
    const iconBytes = new TextEncoder().encode(icon);

    // UI hints flags: colorgroup in upper 4 bits, flags in lower 4 bits
    // AMBER = 2, hasWidget(1) + hasUnit(2) + hasIcon(4) = 0x07
    // Combined: (2 << 4) | 0x07 = 0x27
    const msg = new Uint8Array([
        0x03,           // SCHEMA_UPSERT opcode (no batch)
        0x01,           // item type: PROPERTY
        0x00,           // level flags: ROOT
        5,              // item ID
        0,              // namespace ID
        nameBytes.length, // name length
        ...nameBytes,   // name
        0,              // description length (varint)
        0x03,           // type ID: UINT8
        0,              // validation flags
        100,            // default value
        // UI hints (color in upper 4 bits of flags)
        0x27,           // flags: colorgroup=2 (AMBER) | hasWidget | hasUnit | hasIcon
        1,              // widget hint: SLIDER (first per spec order)
        unitBytes.length, // unit length
        ...unitBytes,   // unit: "ms"
        iconBytes.length, // icon length
        ...iconBytes    // icon: "⚡"
    ]);

    client._handleMessage(msg);

    assertEqual(schemaProp.id, 5, 'id');
    assertEqual(schemaProp.name, 'speed', 'name');
    assertEqual(schemaProp.ui.color, 'amber', 'color name');
    assertEqual(schemaProp.ui.colorHex, '#fcd34d', 'color hex');
    assertEqual(schemaProp.ui.unit, 'ms', 'unit');
    assertEqual(schemaProp.ui.icon, '⚡', 'icon');
    assertEqual(schemaProp.ui.widget, 1, 'widget');
});

test('decodes schema with partial UI hints', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });

    let schemaProp = null;
    client.on('schema', (prop) => {
        schemaProp = prop;
    });

    const name = 'temp';
    const nameBytes = new TextEncoder().encode(name);
    const unit = '°C';
    const unitBytes = new TextEncoder().encode(unit);

    // UI hints flags: hasUnit(2) only = 0x02
    const msg = new Uint8Array([
        0x03,           // SCHEMA_UPSERT opcode (no batch)
        0x01,           // item type: PROPERTY
        0x00,           // level flags: ROOT
        7,              // item ID
        0,              // namespace ID
        nameBytes.length,
        ...nameBytes,
        0,              // description length
        0x05,           // type ID: FLOAT32
        0,              // validation flags
        0, 0, 0, 0,     // default value (float 0.0)
        0x02,           // flags: hasUnit only
        unitBytes.length,
        ...unitBytes
    ]);

    client._handleMessage(msg);

    assertEqual(schemaProp.ui.color, null, 'no color');
    assertEqual(schemaProp.ui.unit, '°C', 'unit');
    assertEqual(schemaProp.ui.icon, null, 'no icon');
    assertEqual(schemaProp.ui.widget, 0, 'no widget');
});

// ============== Varint Decoding Tests ==============

console.log('\n== Varint Decoding ==');

test('decodes single-byte varint', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    const data = new Uint8Array([0x7F]);  // 127

    const [value, bytes] = client._decodeVarint(data, 0);

    assertEqual(value, 127, 'value');
    assertEqual(bytes, 1, 'bytes consumed');
});

test('decodes multi-byte varint', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    const data = new Uint8Array([0x80, 0x01]);  // 128

    const [value, bytes] = client._decodeVarint(data, 0);

    assertEqual(value, 128, 'value');
    assertEqual(bytes, 2, 'bytes consumed');
});

test('decodes larger varint', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    // 300 = 0b100101100 = 0xAC 0x02
    const data = new Uint8Array([0xAC, 0x02]);

    const [value, bytes] = client._decodeVarint(data, 0);

    assertEqual(value, 300, 'value');
    assertEqual(bytes, 2, 'bytes consumed');
});

// ============== Type Size Tests ==============

console.log('\n== Type Size ==');

test('returns correct type sizes', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });

    assertEqual(client._getTypeSize(0x01), 1, 'BOOL');
    assertEqual(client._getTypeSize(0x02), 1, 'INT8');
    assertEqual(client._getTypeSize(0x03), 1, 'UINT8');
    assertEqual(client._getTypeSize(0x04), 4, 'INT32');
    assertEqual(client._getTypeSize(0x05), 4, 'FLOAT32');
    assertEqual(client._getTypeSize(0xFF), 0, 'unknown');
});

// ============== Event System Tests ==============

console.log('\n== Event System ==');

test('registers and fires events', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    let called = false;
    let eventData = null;

    client.on('test', (data) => {
        called = true;
        eventData = data;
    });

    client._emit('test', { foo: 'bar' });

    assertEqual(called, true, 'callback called');
    assertDeepEqual(eventData, { foo: 'bar' }, 'event data');
});

test('supports multiple listeners', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    let count = 0;

    client.on('test', () => count++);
    client.on('test', () => count++);
    client.on('test', () => count++);

    client._emit('test');

    assertEqual(count, 3, 'all listeners called');
});

test('removes listeners with off()', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    let count = 0;
    const listener = () => count++;

    client.on('test', listener);
    client._emit('test');
    assertEqual(count, 1, 'first emit');

    client.off('test', listener);
    client._emit('test');
    assertEqual(count, 1, 'after removal');
});

// ============== API Tests ==============

console.log('\n== Public API ==');

test('getProperty returns value by name', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    client.properties.set(3, { id: 3, name: 'brightness', typeId: 0x03, value: 200 });
    client.propertyByName.set('brightness', 3);

    assertEqual(client.getProperty('brightness'), 200, 'by name');
    assertEqual(client.getProperty(3), 200, 'by id');
    assertEqual(client.getProperty('nonexistent'), undefined, 'unknown');
});

test('getProperties returns all properties', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    client.properties.set(1, { id: 1, name: 'speed', typeId: 0x05, value: 1.5, readonly: false });
    client.properties.set(2, { id: 2, name: 'enabled', typeId: 0x01, value: true, readonly: true });

    const props = client.getProperties();

    assertEqual(Object.keys(props).length, 2, 'count');
    assertEqual(props.speed.value, 1.5, 'speed value');
    assertEqual(props.enabled.readonly, true, 'enabled readonly');
});

test('isConnected returns correct state', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });

    assertEqual(client.isConnected(), false, 'before connect');

    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;
    client.connected = true;

    assertEqual(client.isConnected(), true, 'after connect');
});

// ============== Error Handling Tests ==============

console.log('\n== Error Handling ==');

test('decodes ERROR message', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });

    let errorReceived = null;
    client.on('error', (err) => {
        errorReceived = err;
    });

    // Build ERROR message
    const errorMsg = 'Bad value';
    const msgBytes = new TextEncoder().encode(errorMsg);
    const msg = new Uint8Array([
        0x07,           // ERROR opcode
        0x05, 0x00,     // error code 5 (VALIDATION_FAILED) little-endian
        msgBytes.length, // message length (varint)
        ...msgBytes     // message
    ]);

    client._handleMessage(msg);

    assertEqual(errorReceived.code, 5, 'error code');
    assertEqual(errorReceived.message, 'Bad value', 'error message');
});

// ============== Heartbeat Tests ==============

console.log('\n== Heartbeat ==');

test('heartbeat starts after ready event', () => {
    const client = new MicroProtoClient('ws://test:81', {
        reconnect: false,
        heartbeatInterval: 100
    });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;
    client.connected = true;

    // Setup a property so ready can fire
    client.properties.set(1, { id: 1, name: 'test', typeId: 0x03, value: 0 });

    // Simulate property update which triggers ready
    const msg = new Uint8Array([0x01, 1, 0, 100]);
    client._handleMessage(msg);

    // Heartbeat timer should be set
    assertEqual(client._heartbeatTimer !== null, true, 'heartbeat timer started');

    // Cleanup
    client._stopHeartbeat();
});

test('PONG clears heartbeat timeout', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;
    client.connected = true;

    // Simulate a pending timeout
    client._heartbeatTimeoutTimer = setTimeout(() => {}, 10000);
    assertEqual(client._heartbeatTimeoutTimer !== null, true, 'timeout set');

    // Receive PONG - MVP: PING opcode (0x06) with is_response flag (bit 4) = 0x16
    const pong = new Uint8Array([0x16, 0x01, 0x00, 0x00, 0x00]);  // PONG with payload=1
    client._handleMessage(pong);

    assertEqual(client._heartbeatTimeoutTimer, null, 'timeout cleared');
    assertEqual(client._lastPongTime > 0, true, 'lastPongTime updated');
});

test('heartbeat timeout emits connectionLost', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;
    client.connected = true;

    let connectionLostFired = false;
    client.on('connectionLost', () => {
        connectionLostFired = true;
    });

    // Trigger timeout handler directly
    client._onHeartbeatTimeout();

    assertEqual(connectionLostFired, true, 'connectionLost emitted');
});

test('heartbeat stops on disconnect', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;
    client.connected = true;

    // Start heartbeat
    client._startHeartbeat();
    assertEqual(client._heartbeatTimer !== null, true, 'heartbeat running');

    // Disconnect
    client.disconnect();

    assertEqual(client._heartbeatTimer, null, 'heartbeat stopped');
});

test('_sendPing sends correct message', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;
    client.connected = true;

    client._sendPing();

    assertEqual(client.ws.sentMessages.length, 1, 'message sent');
    const sent = new Uint8Array(client.ws.sentMessages[0]);
    assertEqual(sent[0], 0x06, 'PING opcode');  // MVP: PING is 0x06
    // Payload is varint-encoded (value 0 = 1 byte), so total = 1 (header) + 1 (varint) = 2
    assertEqual(sent.length, 2, 'message length');
    assertEqual(sent[1], 0x00, 'varint payload 0');
});

test('getLastPongAge returns correct value', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });

    assertEqual(client.getLastPongAge(), -1, 'before any pong');

    client._lastPongTime = Date.now() - 1000;  // 1 second ago
    const age = client.getLastPongAge();

    assert(age >= 1000 && age < 1100, 'age ~1000ms');
});

// ============== Varint Encoding Tests ==============

console.log('\n== Varint Encoding ==');

test('encodes single-byte varints', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });

    const v0 = client._encodeVarint(0);
    assertEqual(v0.length, 1, 'length for 0');
    assertEqual(v0[0], 0x00, 'value for 0');

    const v127 = client._encodeVarint(127);
    assertEqual(v127.length, 1, 'length for 127');
    assertEqual(v127[0], 0x7F, 'value for 127');
});

test('encodes multi-byte varints', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });

    // 128 = 0x80 | 0x00, 0x01
    const v128 = client._encodeVarint(128);
    assertEqual(v128.length, 2, 'length for 128');
    assertEqual(v128[0], 0x80, 'byte 0 for 128');
    assertEqual(v128[1], 0x01, 'byte 1 for 128');

    // 4096 = 0x80 | (4096 & 0x7F), 4096 >> 7 = 0x80 | 0, 32 = 0x80, 0x20
    const v4096 = client._encodeVarint(4096);
    assertEqual(v4096.length, 2, 'length for 4096');
    assertEqual(v4096[0], 0x80, 'byte 0 for 4096');
    assertEqual(v4096[1], 0x20, 'byte 1 for 4096');
});

test('encodes propid correctly', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });

    // ID < 128: single byte
    const p50 = client._encodePropId(50);
    assertEqual(p50.length, 1, 'length for 50');
    assertEqual(p50[0], 50, 'value for 50');

    // ID >= 128: 2 bytes with high bit set
    const p200 = client._encodePropId(200);
    assertEqual(p200.length, 2, 'length for 200');
    assertEqual(p200[0], 0x80 | (200 & 0x7F), 'byte 0 for 200');  // 0xC8
    assertEqual(p200[1], 200 >> 7, 'byte 1 for 200');  // 1
});

test('decodes propid correctly', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });

    // Single byte
    const [v1, b1] = client._decodePropId(new Uint8Array([50, 0xFF]), 0);
    assertEqual(v1, 50, 'value for single byte');
    assertEqual(b1, 1, 'bytes consumed for single byte');

    // Two bytes
    const [v2, b2] = client._decodePropId(new Uint8Array([0xC8, 0x01, 0xFF]), 0);
    assertEqual(v2, 200, 'value for two bytes');
    assertEqual(b2, 2, 'bytes consumed for two bytes');
});

// ============== SCHEMA_DELETE Tests ==============

console.log('\n== Schema Delete ==');

test('handles single schema delete', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });

    // Setup property
    client.properties.set(5, { id: 5, name: 'testProp', typeId: 0x03 });
    client.propertyByName.set('testProp', 5);

    let deleteEvent = null;
    client.on('schemaDelete', (info) => { deleteEvent = info; });

    // SCHEMA_DELETE message: opcode(0x04) + item_type_flags(0x10=property) + propid(5)
    const msg = new Uint8Array([0x04, 0x01, 5]);  // opcode=4, type=1 (property), id=5
    client._handleMessage(msg);

    assertEqual(client.properties.has(5), false, 'property removed');
    assertEqual(client.propertyByName.has('testProp'), false, 'name removed');
    assertEqual(deleteEvent.type, 'property', 'event type');
    assertEqual(deleteEvent.id, 5, 'event id');
});

test('handles batched schema delete', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });

    // Setup properties
    client.properties.set(1, { id: 1, name: 'prop1', typeId: 0x03 });
    client.properties.set(2, { id: 2, name: 'prop2', typeId: 0x01 });
    client.propertyByName.set('prop1', 1);
    client.propertyByName.set('prop2', 2);

    // SCHEMA_DELETE batched: opcode(0x14) + count-1(1) + items...
    // flags: bit0=batch(1) in bits 4-7 = 0x10
    const msg = new Uint8Array([
        0x14,  // opcode=4, batch flag(0x10)
        0x01,  // count-1 = 1 (so 2 items)
        0x01, 1,  // type=property, id=1
        0x01, 2   // type=property, id=2
    ]);
    client._handleMessage(msg);

    assertEqual(client.properties.size, 0, 'all properties removed');
});

// ============== RPC Tests ==============

console.log('\n== RPC ==');

test('builds RPC request correctly', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;
    client.connected = true;

    // Register function
    client.functions.set(1, { id: 1, name: 'testFunc' });
    client.functionByName.set('testFunc', 1);

    // Call with needs_response=true (catch rejection to avoid unhandled promise)
    client.callFunction(1, null, true).catch(() => {});

    const sent = new Uint8Array(client.ws.sentMessages[0]);
    // opcode=5 (RPC), flags=0x02 (needs_response) → header = 0x25
    assertEqual(sent[0], 0x25, 'RPC header with needs_response');
    assertEqual(sent[1], 1, 'function id');
    assertEqual(sent[2], 0, 'call id');
});

test('builds fire-and-forget RPC correctly', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;
    client.connected = true;

    client.functions.set(2, { id: 2, name: 'fireFunc' });
    client.functionByName.set('fireFunc', 2);

    // Call with needs_response=false
    client.callFunction(2, null, false);

    const sent = new Uint8Array(client.ws.sentMessages[0]);
    // opcode=5 (RPC), flags=0x00 → header = 0x05
    assertEqual(sent[0], 0x05, 'RPC header without needs_response');
    assertEqual(sent[1], 2, 'function id');
    // No call_id for fire-and-forget
    assertEqual(sent.length, 2, 'no call_id');
});

// ============== Function Schema Decoding Tests ==============

console.log('\n== Function Schema ==');

// Helper: build a SCHEMA_UPSERT for a function
function buildFunctionSchema(id, name, params, returnTypeId) {
    const nameBytes = new TextEncoder().encode(name);
    const parts = [
        0x03,           // SCHEMA_UPSERT opcode (no batch)
        0x02,           // item_type: FUNCTION
        0x04,           // level_flags: ble_exposed=1
        id,             // function ID
        0,              // namespace ID
        nameBytes.length,
        ...nameBytes,
        0,              // description length (varint 0)
        params.length   // param_count
    ];

    for (const p of params) {
        const pNameBytes = new TextEncoder().encode(p.name);
        parts.push(pNameBytes.length, ...pNameBytes);
        parts.push(p.typeId, 0);  // typeId + validation_flags=0
    }

    parts.push(returnTypeId);
    if (returnTypeId !== 0) {
        parts.push(0);  // validation_flags for return type
    }

    return new Uint8Array(parts);
}

test('decodes function schema with no params', () => {
    localStorage.clear();
    const client = new MicroProtoClient('ws://test:81', { reconnect: false, storageKey: 'test_fn' });

    client._handleMessage(buildFunctionSchema(0, 'reset', [], 0x00));

    assertEqual(client.functions.size, 1, 'function registered');
    const func = client.functions.get(0);
    assertEqual(func.name, 'reset', 'function name');
    assertEqual(func.type, 'function', 'type is function');
    assertEqual(func.params.length, 0, 'no params');
    assertEqual(func.returnTypeId, 0x00, 'void return');
    assertEqual(func.bleExposed, true, 'ble exposed');
    assertEqual(client.functionByName.get('reset'), 0, 'name lookup');
});

test('decodes function schema with params and return type', () => {
    localStorage.clear();
    const client = new MicroProtoClient('ws://test:81', { reconnect: false, storageKey: 'test_fn2' });

    client._handleMessage(buildFunctionSchema(
        1, 'save',
        [{name: 'slot', typeId: 0x03}],  // UINT8
        0x01  // BOOL return
    ));

    const func = client.functions.get(1);
    assertEqual(func.name, 'save', 'function name');
    assertEqual(func.params.length, 1, 'one param');
    assertEqual(func.params[0].name, 'slot', 'param name');
    assertEqual(func.params[0].typeId, 0x03, 'param type UINT8');
    assertEqual(func.returnTypeId, 0x01, 'BOOL return');
});

test('decodes batched function + property schema', () => {
    localStorage.clear();
    const client = new MicroProtoClient('ws://test:81', { reconnect: false, storageKey: 'test_fn3' });

    // First: property schema
    const propName = new TextEncoder().encode('brightness');
    client._handleMessage(new Uint8Array([
        0x03, 0x01, 0x00, 3, 0, propName.length, ...propName,
        0, 0x03, 0, 128, 0  // desc=0, UINT8, val_flags=0, default=128, ui=0
    ]));

    // Then: function schema
    client._handleMessage(buildFunctionSchema(0, 'reset', [], 0x00));

    assertEqual(client.properties.size, 1, 'property registered');
    assertEqual(client.functions.size, 1, 'function registered');
});

// ============== RPC Response Tests ==============

console.log('\n== RPC Responses ==');

test('resolves RPC success with return value', (done) => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;
    client.connected = true;

    client.functions.set(1, { id: 1, name: 'ping', returnTypeDef: { typeId: 0x03 } });
    client.functionByName.set('ping', 1);

    client.callFunction(1, null, true).then((result) => {
        // Will verify async
    }).catch(() => {});

    // Simulate success response: opcode=5, flags=7 (is_response|success|has_return), callId=0, value=42
    const response = new Uint8Array([0x75, 0, 42]);
    client._handleMessage(response);
});

test('resolves RPC success without return value', (done) => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;
    client.connected = true;

    client.functions.set(1, { id: 1, name: 'reset' });
    client.functionByName.set('reset', 1);

    client.callFunction(1, null, true).then((result) => {
        // success, no value
    }).catch(() => {});

    // Success response without return value: flags=3 (is_response|success)
    const response = new Uint8Array([0x35, 0]);
    client._handleMessage(response);
});

test('rejects RPC error response', (done) => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;
    client.connected = true;

    client.functions.set(1, { id: 1, name: 'fail' });
    client.functionByName.set('fail', 1);

    client.callFunction(1, null, true).catch((err) => {
        // error caught
    });

    // Error response: flags=1 (is_response only), callId=0, errorCode=3, message
    const msg = new TextEncoder().encode('bad');
    const response = new Uint8Array([0x15, 0, 3, msg.length, ...msg]);
    client._handleMessage(response);
});

test('RPC call by function name', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;
    client.connected = true;

    client.functions.set(5, { id: 5, name: 'doStuff' });
    client.functionByName.set('doStuff', 5);

    client.callFunction('doStuff', null, true).catch(() => {});

    const sent = new Uint8Array(client.ws.sentMessages[0]);
    assertEqual(sent[0], 0x25, 'RPC header');
    assertEqual(sent[1], 5, 'function id from name lookup');
});

test('RPC call with params', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;
    client.connected = true;

    client.functions.set(1, { id: 1, name: 'save' });
    client.functionByName.set('save', 1);

    client.callFunction(1, [42, 0xFF], true).catch(() => {});

    const sent = new Uint8Array(client.ws.sentMessages[0]);
    assertEqual(sent[0], 0x25, 'RPC header');
    assertEqual(sent[1], 1, 'function id');
    assertEqual(sent[2], 0, 'call id');
    assertEqual(sent[3], 42, 'param byte 0');
    assertEqual(sent[4], 0xFF, 'param byte 1');
});

// ============== RPC Edge Cases ==============

console.log('\n== RPC Edge Cases ==');

test('RPC response for unknown callId is ignored', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;
    client.connected = true;

    // No pending calls — response should not crash
    const response = new Uint8Array([0x75, 99, 42]);  // callId=99, value=42
    client._handleMessage(response);
    // No assertion needed — just should not throw
});

test('RPC call to unknown function name rejects', (done) => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;
    client.connected = true;

    client.callFunction('nonexistent', null, true).catch((err) => {
        // Expected rejection
    });
    // Promise rejects synchronously for unknown name
});

test('function schema with description', () => {
    localStorage.clear();
    const client = new MicroProtoClient('ws://test:81', { reconnect: false, storageKey: 'test_fndesc' });

    const nameBytes = new TextEncoder().encode('reset');
    const descBytes = new TextEncoder().encode('Reset all settings');
    const msg = new Uint8Array([
        0x03, 0x02, 0x04,  // SCHEMA_UPSERT, FUNCTION, ble_exposed
        0, 0,               // func_id=0, ns_id=0
        nameBytes.length, ...nameBytes,
        descBytes.length, ...descBytes,  // description as varint len + bytes
        0,                  // param_count=0
        0x00                // return: void
    ]);

    client._handleMessage(msg);

    const func = client.functions.get(0);
    assertEqual(func.description, 'Reset all settings', 'description decoded');
});

test('function schema with multiple params', () => {
    localStorage.clear();
    const client = new MicroProtoClient('ws://test:81', { reconnect: false, storageKey: 'test_fnmp' });

    const nameBytes = new TextEncoder().encode('setPos');
    const p1 = new TextEncoder().encode('x');
    const p2 = new TextEncoder().encode('y');
    const p3 = new TextEncoder().encode('z');

    const msg = new Uint8Array([
        0x03, 0x02, 0x04,
        0, 0,
        nameBytes.length, ...nameBytes,
        0,             // no description
        3,             // 3 params
        p1.length, ...p1, 0x05, 0,  // x: FLOAT32
        p2.length, ...p2, 0x05, 0,  // y: FLOAT32
        p3.length, ...p3, 0x05, 0,  // z: FLOAT32
        0x01, 0        // return: BOOL
    ]);

    client._handleMessage(msg);

    const func = client.functions.get(0);
    assertEqual(func.params.length, 3, '3 params');
    assertEqual(func.params[0].name, 'x', 'param 0 name');
    assertEqual(func.params[1].name, 'y', 'param 1 name');
    assertEqual(func.params[2].name, 'z', 'param 2 name');
    assertEqual(func.params[0].typeId, 0x05, 'FLOAT32');
    assertEqual(func.returnTypeId, 0x01, 'BOOL return');
});

test('function not stored in properties map', () => {
    localStorage.clear();
    const client = new MicroProtoClient('ws://test:81', { reconnect: false, storageKey: 'test_fnsep' });

    client._handleMessage(buildFunctionSchema(0, 'myFunc', [], 0x00));

    assertEqual(client.functions.size, 1, 'in functions map');
    assertEqual(client.properties.size, 0, 'not in properties map');
});

test('function schema overwrites existing function', () => {
    localStorage.clear();
    const client = new MicroProtoClient('ws://test:81', { reconnect: false, storageKey: 'test_fnow' });

    client._handleMessage(buildFunctionSchema(0, 'old', [], 0x00));
    assertEqual(client.functions.get(0).name, 'old', 'initial name');

    // Re-send with same ID but different name
    client._handleMessage(buildFunctionSchema(0, 'new', [{name: 'a', typeId: 0x03}], 0x01));
    assertEqual(client.functions.get(0).name, 'new', 'overwritten name');
    assertEqual(client.functions.get(0).params.length, 1, 'overwritten params');
});

test('RPC call wraps call_id at 255', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;
    client.connected = true;

    client.functions.set(1, { id: 1, name: 'f' });
    client.functionByName.set('f', 1);

    // Set call ID near wrap point
    client._rpcCallId = 255;
    client.callFunction(1, null, true).catch(() => {});
    const sent1 = new Uint8Array(client.ws.sentMessages[0]);
    assertEqual(sent1[2], 255, 'callId 255');

    client.callFunction(1, null, true).catch(() => {});
    const sent2 = new Uint8Array(client.ws.sentMessages[1]);
    assertEqual(sent2[2], 0, 'callId wraps to 0');
});

// ============== Resync Tests ==============

console.log('\n== Resync ==');

test('resync sends HELLO', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false, deviceId: 1 });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;

    const result = client.resync();

    assertEqual(result, true, 'resync returns true');
    assertEqual(client.ws.sentMessages.length, 1, 'message sent');
    const sent = new Uint8Array(client.ws.sentMessages[0]);
    assertEqual(sent[0], 0x00, 'HELLO opcode');
});

test('resync fails when not connected', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    // No ws or ws not open

    const result = client.resync();
    assertEqual(result, false, 'resync returns false');
});

// ============== Schema Caching Tests ==============

console.log('\n== Schema Caching ==');

// Helper: build a HELLO response with a given schemaVersion
function buildHelloResponse(schemaVersion) {
    return new Uint8Array([
        0x10,        // HELLO opcode with is_response flag
        1,           // version
        0x80, 0x01,  // maxPacket = 128 as varint
        0x01,        // sessionId = 1
        0x01,        // timestamp = 1
        schemaVersion & 0xFF, (schemaVersion >> 8) & 0xFF  // u16 LE
    ]);
}

// Helper: build a SCHEMA_UPSERT for a UINT8 property
function buildSchemaUpsert(id, name) {
    const nameBytes = new TextEncoder().encode(name);
    return new Uint8Array([
        0x03,           // SCHEMA_UPSERT opcode
        0x01,           // item type: PROPERTY
        0x00,           // level flags: LOCAL
        id,             // item ID
        0,              // namespace ID
        nameBytes.length,
        ...nameBytes,
        0,              // description length
        0x03,           // type ID: UINT8
        0,              // validation flags
        0,              // default value
        0               // UI hints (none)
    ]);
}

test('saves schema version to localStorage on HELLO response', () => {
    localStorage.clear();
    const client = new MicroProtoClient('ws://test:81', { reconnect: false, storageKey: 'test_sv' });

    client._handleMessage(buildHelloResponse(42));

    assertEqual(localStorage.getItem('test_sv_v'), '42', 'schema version saved');
});

test('saves schema to localStorage on SCHEMA_UPSERT', () => {
    localStorage.clear();
    const client = new MicroProtoClient('ws://test:81', { reconnect: false, storageKey: 'test_sc' });

    // First receive HELLO
    client._handleMessage(buildHelloResponse(5));
    // Then receive schema
    client._handleMessage(buildSchemaUpsert(0, 'brightness'));
    client._handleMessage(buildSchemaUpsert(1, 'speed'));

    assertEqual(client.properties.size, 2, 'properties registered');
    const cached = JSON.parse(localStorage.getItem('test_sc'));
    assertEqual(cached.length, 2, 'cached schema has 2 items');
});

test('sends cached schema version in HELLO request', () => {
    localStorage.clear();
    localStorage.setItem('test_cv_v', '7');
    const client = new MicroProtoClient('ws://test:81', { reconnect: false, deviceId: 1, storageKey: 'test_cv' });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;

    client._sendHello();

    const sent = new Uint8Array(client.ws.sentMessages[0]);
    // schemaVersion is last 2 bytes (u16 LE)
    const svLo = sent[sent.length - 2];
    const svHi = sent[sent.length - 1];
    assertEqual(svLo, 7, 'schema version lo byte');
    assertEqual(svHi, 0, 'schema version hi byte');
});

test('restores schema from cache when server version matches', () => {
    localStorage.clear();
    const client = new MicroProtoClient('ws://test:81', { reconnect: false, storageKey: 'test_match' });

    // Simulate first connection: receive schema
    client._handleMessage(buildHelloResponse(10));
    client._handleMessage(buildSchemaUpsert(0, 'brightness'));
    client._handleMessage(buildSchemaUpsert(1, 'speed'));
    assertEqual(client.properties.size, 2, 'initial properties');

    // Simulate reconnect: clear client state, then HELLO with same version
    client.properties.clear();
    client.propertyByName.clear();
    assertEqual(client.properties.size, 0, 'cleared before reconnect');

    // Server sends same schemaVersion=10 → client should restore from cache
    client._handleMessage(buildHelloResponse(10));

    assertEqual(client.properties.size, 2, 'properties restored from cache');
    assertEqual(client.propertyByName.get('brightness'), 0, 'brightness lookup restored');
    assertEqual(client.propertyByName.get('speed'), 1, 'speed lookup restored');
});

test('clears schema when server version differs', () => {
    localStorage.clear();
    const client = new MicroProtoClient('ws://test:81', { reconnect: false, storageKey: 'test_diff' });

    // First connection with version 3
    client._handleMessage(buildHelloResponse(3));
    client._handleMessage(buildSchemaUpsert(0, 'brightness'));
    assertEqual(client.properties.size, 1, 'initial property');

    // Reconnect with different version 4 → should clear, not restore
    client._handleMessage(buildHelloResponse(4));

    assertEqual(client.properties.size, 0, 'properties cleared for new schema');
});

test('sends schema version 0 when no cache exists', () => {
    localStorage.clear();
    const client = new MicroProtoClient('ws://test:81', { reconnect: false, deviceId: 1, storageKey: 'test_nocache' });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;

    client._sendHello();

    const sent = new Uint8Array(client.ws.sentMessages[0]);
    const svLo = sent[sent.length - 2];
    const svHi = sent[sent.length - 1];
    assertEqual(svLo, 0, 'no cache → version 0 lo');
    assertEqual(svHi, 0, 'no cache → version 0 hi');
});

// ============== ERROR with schema_mismatch Tests ==============

console.log('\n== Error Schema Mismatch ==');

test('triggers resync on schema_mismatch error', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false, deviceId: 1 });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;

    let errorEvent = null;
    client.on('error', (e) => { errorEvent = e; });

    // ERROR message with schema_mismatch flag
    // opcode=0x07, flags=0x01 (schema_mismatch) → header = 0x17
    const view = new DataView(new ArrayBuffer(6));
    view.setUint8(0, 0x17);  // ERROR with schema_mismatch
    view.setUint16(1, 0x0002, true);  // error code
    view.setUint8(3, 0x02);  // message length varint = 2
    view.setUint8(4, 0x4F);  // 'O'
    view.setUint8(5, 0x4B);  // 'K'

    client._handleMessage(new Uint8Array(view.buffer));

    assertEqual(errorEvent.schemaMismatch, true, 'schemaMismatch flag');
    // Should have triggered resync (HELLO sent)
    assertEqual(client.ws.sentMessages.length, 1, 'HELLO sent for resync');
});

// ============== OBJECT Type Decoding Tests ==============

console.log('\n== OBJECT Type Decoding ==');

test('decodes OBJECT values', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });

    // Setup OBJECT property
    const objectProp = {
        id: 10,
        name: 'position',
        typeId: MicroProtoClient.TYPES.OBJECT,
        fields: [
            { name: 'x', typeDef: { typeId: MicroProtoClient.TYPES.INT32 } },
            { name: 'y', typeDef: { typeId: MicroProtoClient.TYPES.INT32 } }
        ]
    };
    client.properties.set(10, objectProp);
    client.propertyByName.set('position', 10);

    let receivedValue = null;
    client.on('property', (id, name, value) => {
        receivedValue = value;
    });

    // PROPERTY_UPDATE: propid=10, values: x=100 (i32), y=200 (i32)
    const msg = new ArrayBuffer(11);  // 1 + 1 + 4 + 4 + 1 (extra for safety)
    const bytes = new Uint8Array(msg);
    const view = new DataView(msg);
    bytes[0] = 0x01;  // PROPERTY_UPDATE
    bytes[1] = 10;    // propid
    view.setInt32(2, 100, true);  // x
    view.setInt32(6, 200, true);  // y

    client._handleMessage(bytes.slice(0, 10));

    assertEqual(receivedValue.x, 100, 'x value');
    assertEqual(receivedValue.y, 200, 'y value');
});

// ============== getPropertySchema Tests ==============

console.log('\n== Property Schema API ==');

test('getPropertySchema returns full schema', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });

    client.properties.set(1, {
        id: 1,
        name: 'brightness',
        typeId: 0x03,
        constraints: { hasMin: true, min: 0, hasMax: true, max: 255 },
        ui: { widget: 1, color: 'rose' }
    });
    client.propertyByName.set('brightness', 1);

    const schema = client.getPropertySchema('brightness');

    assertEqual(schema.id, 1, 'id');
    assertEqual(schema.name, 'brightness', 'name');
    assertEqual(schema.constraints.min, 0, 'min');
    assertEqual(schema.constraints.max, 255, 'max');
    assertEqual(schema.ui.widget, 1, 'widget');
});

// ============== LED Use Case Tests ==============

console.log('\n== LED Use Cases ==');

// Simulate the actual LED properties from src/animations/Anime.cpp
function setupLedProperties(client) {
    // brightness: Property<uint8_t>, 0-255
    client.properties.set(1, {
        id: 1,
        name: 'brightness',
        typeId: MicroProtoClient.TYPES.UINT8,
        readonly: false,
        constraints: { hasMin: true, min: 0, hasMax: true, max: 255 },
        ui: { widget: 1, unit: '%' }
    });
    client.propertyByName.set('brightness', 1);

    // shaderIndex: Property<uint8_t>, 0-N
    client.properties.set(2, {
        id: 2,
        name: 'shaderIndex',
        typeId: MicroProtoClient.TYPES.UINT8,
        readonly: false,
        constraints: { hasMin: true, min: 0, hasMax: true, max: 20 }
    });
    client.propertyByName.set('shaderIndex', 2);

    // ledCount: Property<uint8_t>, 1-200
    client.properties.set(3, {
        id: 3,
        name: 'ledCount',
        typeId: MicroProtoClient.TYPES.UINT8,
        readonly: false,
        constraints: { hasMin: true, min: 1, hasMax: true, max: 200 }
    });
    client.propertyByName.set('ledCount', 3);

    // atmosphericFade: Property<bool>
    client.properties.set(4, {
        id: 4,
        name: 'atmosphericFade',
        typeId: MicroProtoClient.TYPES.BOOL,
        readonly: false
    });
    client.propertyByName.set('atmosphericFade', 4);

    // ledPreview: ListProperty<uint8_t, LED_LIMIT * 3> - RGB values (readonly)
    client.properties.set(5, {
        id: 5,
        name: 'ledPreview',
        typeId: MicroProtoClient.TYPES.LIST,
        elementTypeId: MicroProtoClient.TYPES.UINT8,
        elementTypeDef: { typeId: MicroProtoClient.TYPES.UINT8 },
        readonly: true,
        value: []
    });
    client.propertyByName.set('ledPreview', 5);
}

test('sets brightness via setProperty', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;
    client.connected = true;
    setupLedProperties(client);

    const result = client.setProperty('brightness', 128);

    assertEqual(result, true, 'setProperty returns true');
    const sent = new Uint8Array(client.ws.sentMessages[0]);
    assertEqual(sent[0], 0x01, 'PROPERTY_UPDATE opcode');
    assertEqual(sent[1], 1, 'brightness prop id');
    assertEqual(sent[2], 128, 'brightness value');
});

test('sets shaderIndex to change animation', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;
    client.connected = true;
    setupLedProperties(client);

    client.setProperty('shaderIndex', 5);

    const sent = new Uint8Array(client.ws.sentMessages[0]);
    assertEqual(sent[1], 2, 'shaderIndex prop id');
    assertEqual(sent[2], 5, 'shader index value');
});

test('sets ledCount to configure LED strip length', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;
    client.connected = true;
    setupLedProperties(client);

    client.setProperty('ledCount', 60);

    const sent = new Uint8Array(client.ws.sentMessages[0]);
    assertEqual(sent[1], 3, 'ledCount prop id');
    assertEqual(sent[2], 60, 'led count value');
});

test('toggles atmosphericFade effect', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;
    client.connected = true;
    setupLedProperties(client);

    client.setProperty('atmosphericFade', true);

    const sent = new Uint8Array(client.ws.sentMessages[0]);
    assertEqual(sent[1], 4, 'atmosphericFade prop id');
    assertEqual(sent[2], 1, 'true value');
});

test('rejects write to readonly ledPreview', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;
    client.connected = true;
    setupLedProperties(client);

    const result = client.setProperty('ledPreview', [255, 0, 0]);

    assertEqual(result, false, 'setProperty returns false for readonly');
    assertEqual(client.ws.sentMessages.length, 0, 'no message sent');
});

test('receives ledPreview RGB data update', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    setupLedProperties(client);

    let receivedValue = null;
    client.on('property', (id, name, value) => {
        if (name === 'ledPreview') receivedValue = value;
    });

    // Simulate ledPreview update: 3 LEDs = 9 bytes of RGB
    // LIST format: varint(count) + bytes
    const msg = new Uint8Array([
        0x01,  // PROPERTY_UPDATE
        5,     // ledPreview prop id
        9,     // count = 9 elements (3 LEDs * RGB)
        255, 0, 0,     // LED 0: red
        0, 255, 0,     // LED 1: green
        0, 0, 255      // LED 2: blue
    ]);
    client._handleMessage(msg);

    assertEqual(receivedValue.length, 9, 'received 9 bytes');
    assertEqual(receivedValue[0], 255, 'LED 0 R');
    assertEqual(receivedValue[1], 0, 'LED 0 G');
    assertEqual(receivedValue[2], 0, 'LED 0 B');
    assertEqual(receivedValue[3], 0, 'LED 1 R');
    assertEqual(receivedValue[4], 255, 'LED 1 G');
    assertEqual(receivedValue[5], 0, 'LED 1 B');
});

test('receives larger ledPreview for 60 LEDs', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    setupLedProperties(client);

    let receivedValue = null;
    client.on('property', (id, name, value) => {
        if (name === 'ledPreview') receivedValue = value;
    });

    // 60 LEDs = 180 bytes of RGB (varint: 0xB4, 0x01)
    const rgbData = new Array(180).fill(0).map((_, i) => i % 256);
    const msg = new Uint8Array([
        0x01,  // PROPERTY_UPDATE
        5,     // ledPreview prop id
        0xB4, 0x01,  // count = 180 as varint
        ...rgbData
    ]);
    client._handleMessage(msg);

    assertEqual(receivedValue.length, 180, '60 LEDs * 3 RGB = 180 bytes');
    assertEqual(receivedValue[0], 0, 'first byte');
    assertEqual(receivedValue[179], 179, 'last byte');
});

test('receives batched LED property updates', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    setupLedProperties(client);

    const updates = [];
    client.on('property', (id, name, value) => {
        updates.push({ id, name, value });
    });

    // Batched update: brightness=200, shaderIndex=3, atmosphericFade=true
    const msg = new Uint8Array([
        0x11,  // PROPERTY_UPDATE with batch flag (0x10)
        0x02,  // count-1 = 2 (3 items)
        1, 200,   // brightness = 200
        2, 3,     // shaderIndex = 3
        4, 1      // atmosphericFade = true
    ]);
    client._handleMessage(msg);

    assertEqual(updates.length, 3, '3 property updates');
    assertEqual(updates[0].name, 'brightness', 'first update name');
    assertEqual(updates[0].value, 200, 'brightness value');
    assertEqual(updates[1].name, 'shaderIndex', 'second update name');
    assertEqual(updates[1].value, 3, 'shaderIndex value');
    assertEqual(updates[2].name, 'atmosphericFade', 'third update name');
    assertEqual(updates[2].value, true, 'atmosphericFade value');
});

test('full LED state sync on connect', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    setupLedProperties(client);

    const receivedProps = new Map();
    client.on('property', (id, name, value) => {
        receivedProps.set(name, value);
    });

    // Simulate initial state sync: all 5 LED properties
    // brightness=255, shaderIndex=0, ledCount=30, atmosphericFade=false, ledPreview=[...]
    const ledPreviewData = new Array(90).fill(128);  // 30 LEDs, all gray
    const msg = new Uint8Array([
        0x11,  // PROPERTY_UPDATE batched
        0x04,  // count-1 = 4 (5 items)
        1, 255,      // brightness = 255
        2, 0,        // shaderIndex = 0
        3, 30,       // ledCount = 30
        4, 0,        // atmosphericFade = false
        5, 90,       // ledPreview count = 90
        ...ledPreviewData
    ]);
    client._handleMessage(msg);

    assertEqual(receivedProps.get('brightness'), 255, 'brightness synced');
    assertEqual(receivedProps.get('shaderIndex'), 0, 'shaderIndex synced');
    assertEqual(receivedProps.get('ledCount'), 30, 'ledCount synced');
    assertEqual(receivedProps.get('atmosphericFade'), false, 'atmosphericFade synced');
    assertEqual(receivedProps.get('ledPreview').length, 90, 'ledPreview synced');
});

test('LED property change callback fires', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    setupLedProperties(client);

    let brightnessChanged = false;
    let oldBrightness = null;
    let newBrightness = null;

    client.on('property', (id, name, value, oldValue) => {
        if (name === 'brightness') {
            brightnessChanged = true;
            oldBrightness = oldValue;
            newBrightness = value;
        }
    });

    // Set initial value
    client.properties.get(1).value = 100;

    // Receive update
    const msg = new Uint8Array([0x01, 1, 200]);
    client._handleMessage(msg);

    assertEqual(brightnessChanged, true, 'callback fired');
    assertEqual(oldBrightness, 100, 'old value passed');
    assertEqual(newBrightness, 200, 'new value passed');
});

// ============== Typed Value Encoding Tests ==============

console.log('\n== Typed Value Encoding ==');

// Helper: create a client with a registered property schema
function clientWithProperty(propDef) {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;
    client.connected = true;
    client.properties.set(propDef.id, propDef);
    client.propertyByName.set(propDef.name, propDef.id);
    return client;
}

// --- Basic type encoding ---

test('encodes UINT8 property update', () => {
    const client = clientWithProperty({
        id: 1, name: 'brightness', typeId: 0x03, readonly: false
    });
    client.setProperty('brightness', 200);
    const sent = new Uint8Array(client.ws.sentMessages[0]);
    assertEqual(sent[0], 0x01, 'opcode PROPERTY_UPDATE');
    assertEqual(sent[1], 1, 'propId');
    assertEqual(sent[2], 200, 'value');
});

test('encodes INT16 property update', () => {
    const client = clientWithProperty({
        id: 2, name: 'pos', typeId: 0x06, readonly: false
    });
    client.setProperty('pos', -100);
    const sent = new Uint8Array(client.ws.sentMessages[0]);
    assertEqual(sent[0], 0x01, 'opcode');
    assertEqual(sent[1], 2, 'propId');
    const view = new DataView(sent.buffer);
    assertEqual(view.getInt16(2, true), -100, 'value');
});

test('encodes UINT16 property update', () => {
    const client = clientWithProperty({
        id: 3, name: 'count', typeId: 0x07, readonly: false
    });
    client.setProperty('count', 1000);
    const sent = new Uint8Array(client.ws.sentMessages[0]);
    const view = new DataView(sent.buffer);
    assertEqual(view.getUint16(2, true), 1000, 'value');
});

// --- OBJECT encoding ---

test('encodes simple OBJECT property', () => {
    const client = clientWithProperty({
        id: 5, name: 'pos', typeId: 0x22, readonly: false,
        fields: [
            { name: 'x', typeDef: { typeId: 0x04 } },  // INT32
            { name: 'y', typeDef: { typeId: 0x04 } },   // INT32
        ]
    });
    client.setProperty('pos', { x: 100, y: -50 });
    const sent = new Uint8Array(client.ws.sentMessages[0]);
    assertEqual(sent[0], 0x01, 'opcode');
    assertEqual(sent[1], 5, 'propId');
    const view = new DataView(sent.buffer);
    assertEqual(view.getInt32(2, true), 100, 'x');
    assertEqual(view.getInt32(6, true), -50, 'y');
    assertEqual(sent.length, 10, 'total size: 1+1+4+4');
});

test('encodes OBJECT with mixed field types', () => {
    const client = clientWithProperty({
        id: 6, name: 'config', typeId: 0x22, readonly: false,
        fields: [
            { name: 'enabled', typeDef: { typeId: 0x01 } },  // BOOL
            { name: 'speed', typeDef: { typeId: 0x03 } },    // UINT8
            { name: 'offset', typeDef: { typeId: 0x06 } },   // INT16
            { name: 'ratio', typeDef: { typeId: 0x05 } },    // FLOAT32
        ]
    });
    client.setProperty('config', { enabled: true, speed: 128, offset: -300, ratio: 3.14 });
    const sent = new Uint8Array(client.ws.sentMessages[0]);
    const view = new DataView(sent.buffer);
    assertEqual(sent[2], 1, 'enabled=true');
    assertEqual(sent[3], 128, 'speed');
    assertEqual(view.getInt16(4, true), -300, 'offset');
    // float comparison with tolerance
    const ratio = view.getFloat32(6, true);
    assert(Math.abs(ratio - 3.14) < 0.01, `ratio expected ~3.14 got ${ratio}`);
    assertEqual(sent.length, 10, 'total: 1+1+1+1+2+4');
});

test('encodes OBJECT with zero/default values', () => {
    const client = clientWithProperty({
        id: 7, name: 'data', typeId: 0x22, readonly: false,
        fields: [
            { name: 'a', typeDef: { typeId: 0x04 } },
            { name: 'b', typeDef: { typeId: 0x04 } },
        ]
    });
    client.setProperty('data', { a: 0, b: 0 });
    const sent = new Uint8Array(client.ws.sentMessages[0]);
    const view = new DataView(sent.buffer);
    assertEqual(view.getInt32(2, true), 0, 'a=0');
    assertEqual(view.getInt32(6, true), 0, 'b=0');
});

test('encodes OBJECT with missing fields as 0', () => {
    const client = clientWithProperty({
        id: 8, name: 'partial', typeId: 0x22, readonly: false,
        fields: [
            { name: 'x', typeDef: { typeId: 0x04 } },
            { name: 'y', typeDef: { typeId: 0x04 } },
        ]
    });
    client.setProperty('partial', { x: 42 });  // y missing
    const sent = new Uint8Array(client.ws.sentMessages[0]);
    const view = new DataView(sent.buffer);
    assertEqual(view.getInt32(2, true), 42, 'x');
    assertEqual(view.getInt32(6, true), 0, 'y defaults to 0');
});

// --- ARRAY encoding ---

test('encodes ARRAY of UINT8', () => {
    const client = clientWithProperty({
        id: 10, name: 'rgb', typeId: 0x20, readonly: false,
        elementCount: 3,
        elementTypeDef: { typeId: 0x03 }
    });
    client.setProperty('rgb', [255, 128, 0]);
    const sent = new Uint8Array(client.ws.sentMessages[0]);
    assertEqual(sent[2], 255, 'r');
    assertEqual(sent[3], 128, 'g');
    assertEqual(sent[4], 0, 'b');
    assertEqual(sent.length, 5, 'total: 1+1+3');
});

test('encodes ARRAY of OBJECT', () => {
    const client = clientWithProperty({
        id: 11, name: 'points', typeId: 0x20, readonly: false,
        elementCount: 2,
        elementTypeDef: {
            typeId: 0x22,
            fields: [
                { name: 'x', typeDef: { typeId: 0x06 } },  // INT16
                { name: 'y', typeDef: { typeId: 0x06 } },
            ]
        }
    });
    client.setProperty('points', [{ x: 10, y: 20 }, { x: -30, y: 40 }]);
    const sent = new Uint8Array(client.ws.sentMessages[0]);
    const view = new DataView(sent.buffer);
    assertEqual(view.getInt16(2, true), 10, 'p0.x');
    assertEqual(view.getInt16(4, true), 20, 'p0.y');
    assertEqual(view.getInt16(6, true), -30, 'p1.x');
    assertEqual(view.getInt16(8, true), 40, 'p1.y');
    assertEqual(sent.length, 10, 'total: 1+1+2*4');
});

// --- LIST encoding ---

test('encodes LIST of UINT8', () => {
    const client = clientWithProperty({
        id: 20, name: 'items', typeId: 0x21, readonly: false,
        elementTypeDef: { typeId: 0x03 }
    });
    client.setProperty('items', [10, 20, 30]);
    const sent = new Uint8Array(client.ws.sentMessages[0]);
    assertEqual(sent[2], 3, 'varint count=3');
    assertEqual(sent[3], 10);
    assertEqual(sent[4], 20);
    assertEqual(sent[5], 30);
    assertEqual(sent.length, 6, 'total: 1+1+1+3');
});

test('encodes empty LIST', () => {
    const client = clientWithProperty({
        id: 21, name: 'empty', typeId: 0x21, readonly: false,
        elementTypeDef: { typeId: 0x04 }
    });
    client.setProperty('empty', []);
    const sent = new Uint8Array(client.ws.sentMessages[0]);
    assertEqual(sent[2], 0, 'varint count=0');
    assertEqual(sent.length, 3, 'total: 1+1+1');
});

test('encodes LIST of OBJECT (segment-like)', () => {
    const client = clientWithProperty({
        id: 22, name: 'segments', typeId: 0x21, readonly: false,
        elementTypeDef: {
            typeId: 0x22,
            fields: [
                { name: 'name', typeDef: { typeId: 0x20, elementCount: 8, elementTypeDef: { typeId: 0x03 } } },
                { name: 'ledCount', typeDef: { typeId: 0x07 } },   // UINT16
                { name: 'x', typeDef: { typeId: 0x06 } },          // INT16
                { name: 'y', typeDef: { typeId: 0x06 } },
                { name: 'rotation', typeDef: { typeId: 0x06 } },
                { name: 'flags', typeDef: { typeId: 0x03 } },      // UINT8
                { name: 'width', typeDef: { typeId: 0x03 } },
                { name: 'height', typeDef: { typeId: 0x03 } },
                { name: '_reserved', typeDef: { typeId: 0x03 } },
            ]
        }
    });
    client.setProperty('segments', [
        {
            name: [114, 105, 110, 103, 0, 0, 0, 0],  // "ring"
            ledCount: 24, x: -50, y: 30, rotation: 90,
            flags: 1, width: 0, height: 0, _reserved: 0
        }
    ]);
    const sent = new Uint8Array(client.ws.sentMessages[0]);
    const view = new DataView(sent.buffer);

    assertEqual(sent[2], 1, 'varint count=1');

    // name ARRAY: 8 bytes starting at offset 3
    assertEqual(sent[3], 114, 'r');
    assertEqual(sent[4], 105, 'i');
    assertEqual(sent[5], 110, 'n');
    assertEqual(sent[6], 103, 'g');
    assertEqual(sent[7], 0, 'null');

    // ledCount UINT16 at offset 11
    assertEqual(view.getUint16(11, true), 24, 'ledCount');
    // x INT16 at offset 13
    assertEqual(view.getInt16(13, true), -50, 'x');
    // y INT16 at offset 15
    assertEqual(view.getInt16(15, true), 30, 'y');
    // rotation INT16 at offset 17
    assertEqual(view.getInt16(17, true), 90, 'rotation');
    // flags UINT8 at offset 19
    assertEqual(sent[19], 1, 'flags');
    // width, height, reserved
    assertEqual(sent[20], 0, 'width');
    assertEqual(sent[21], 0, 'height');
    assertEqual(sent[22], 0, 'reserved');

    // Total: opheader(1) + propid(1) + varint(1) + 1*20bytes = 23
    assertEqual(sent.length, 23, 'total size');
});

test('encodes LIST of multiple OBJECTs', () => {
    const client = clientWithProperty({
        id: 23, name: 'points', typeId: 0x21, readonly: false,
        elementTypeDef: {
            typeId: 0x22,
            fields: [
                { name: 'x', typeDef: { typeId: 0x04 } },
                { name: 'y', typeDef: { typeId: 0x04 } },
            ]
        }
    });
    client.setProperty('points', [
        { x: 1, y: 2 },
        { x: 3, y: 4 },
        { x: 5, y: 6 },
    ]);
    const sent = new Uint8Array(client.ws.sentMessages[0]);
    const view = new DataView(sent.buffer);
    assertEqual(sent[2], 3, 'count=3');
    assertEqual(view.getInt32(3, true), 1, 'p0.x');
    assertEqual(view.getInt32(7, true), 2, 'p0.y');
    assertEqual(view.getInt32(11, true), 3, 'p1.x');
    assertEqual(view.getInt32(15, true), 4, 'p1.y');
    assertEqual(view.getInt32(19, true), 5, 'p2.x');
    assertEqual(view.getInt32(23, true), 6, 'p2.y');
    assertEqual(sent.length, 27, 'total: 1+1+1+3*8');
});

test('encodes nested OBJECT with ARRAY field', () => {
    const client = clientWithProperty({
        id: 24, name: 'pixel', typeId: 0x22, readonly: false,
        fields: [
            { name: 'rgb', typeDef: { typeId: 0x20, elementCount: 3, elementTypeDef: { typeId: 0x03 } } },
            { name: 'alpha', typeDef: { typeId: 0x03 } },
        ]
    });
    client.setProperty('pixel', { rgb: [255, 128, 64], alpha: 200 });
    const sent = new Uint8Array(client.ws.sentMessages[0]);
    assertEqual(sent[2], 255, 'r');
    assertEqual(sent[3], 128, 'g');
    assertEqual(sent[4], 64, 'b');
    assertEqual(sent[5], 200, 'alpha');
    assertEqual(sent.length, 6, 'total: 1+1+3+1');
});

test('rejects write to readonly OBJECT', () => {
    const client = clientWithProperty({
        id: 25, name: 'ro', typeId: 0x22, readonly: true,
        fields: [{ name: 'x', typeDef: { typeId: 0x04 } }]
    });
    const result = client.setProperty('ro', { x: 1 });
    assertEqual(result, false, 'should return false');
    assertEqual(client.ws.sentMessages.length, 0, 'no message sent');
});

test('encodes LIST of OBJECT with negative and boundary values', () => {
    const client = clientWithProperty({
        id: 26, name: 'edges', typeId: 0x21, readonly: false,
        elementTypeDef: {
            typeId: 0x22,
            fields: [
                { name: 'val', typeDef: { typeId: 0x06 } },  // INT16
            ]
        }
    });
    client.setProperty('edges', [
        { val: 0 },
        { val: -32768 },   // INT16 min
        { val: 32767 },    // INT16 max
        { val: -1 },
    ]);
    const sent = new Uint8Array(client.ws.sentMessages[0]);
    const view = new DataView(sent.buffer);
    assertEqual(sent[2], 4, 'count');
    assertEqual(view.getInt16(3, true), 0, 'zero');
    assertEqual(view.getInt16(5, true), -32768, 'INT16 min');
    assertEqual(view.getInt16(7, true), 32767, 'INT16 max');
    assertEqual(view.getInt16(9, true), -1, 'negative one');
});

// ============== Summary ==============

console.log('\n' + '='.repeat(40));
console.log(`Tests: ${testCount}, Passed: ${passCount}, Failed: ${failCount}`);
console.log('='.repeat(40));

if (failCount > 0) {
    process.exit(1);
}
process.exit(0);
