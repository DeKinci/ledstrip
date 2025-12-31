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
    // MVP format: opcode(1) + version(1) + maxPacketSize(varint) + deviceId(varint)
    // maxPacketSize = 4096 = varint(0x80, 0x20), deviceId = 1 = varint(0x01)
    assertEqual(sent.length, 5, 'HELLO length');  // 1 + 1 + 2 + 1 = 5
    assertEqual(sent[0], 0x00, 'opcode');  // HELLO
    assertEqual(sent[1], 1, 'version');    // Protocol version 1
    // maxPacketSize = 4096 as varint: 0x80 | (4096 & 0x7F) = 0x80, 4096 >> 7 = 32
    assertEqual(sent[2], 0x80, 'maxPacket varint byte 0');
    assertEqual(sent[3], 0x20, 'maxPacket varint byte 1');
    assertEqual(sent[4], 0x01, 'deviceId varint');
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
    // Length: 1 (opcode) + 1 (version) + 2 (maxPacketSize varint) + 2 (deviceId varint) = 6
    assertEqual(sent.length, 6, 'message length');
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
            0xC8, 0x01   // timestamp = 200 as varint
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
    assertEqual(sent.length, 5, 'message length');
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

// ============== Summary ==============

console.log('\n' + '='.repeat(40));
console.log(`Tests: ${testCount}, Passed: ${passCount}, Failed: ${failCount}`);
console.log('='.repeat(40));

if (failCount > 0) {
    process.exit(1);
}
