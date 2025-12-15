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
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;

    client._sendHello();

    const sent = new Uint8Array(client.ws.sentMessages[0]);
    assertEqual(sent.length, 8, 'HELLO length');
    assertEqual(sent[0], 0x00, 'opcode');  // HELLO
    assertEqual(sent[1], 1, 'version');    // Protocol version 1
    // maxPacketSize = 4096 = 0x1000 little-endian
    assertEqual(sent[2], 0x00, 'maxPacket low');
    assertEqual(sent[3], 0x10, 'maxPacket high');
});

test('encodes custom device ID', () => {
    const client = new MicroProtoClient('ws://test:81', {
        reconnect: false,
        deviceId: 0x12345678
    });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;

    client._sendHello();

    const sent = new Uint8Array(client.ws.sentMessages[0]);
    // deviceId 0x12345678 little-endian at offset 4
    assertEqual(sent[4], 0x78, 'deviceId byte 0');
    assertEqual(sent[5], 0x56, 'deviceId byte 1');
    assertEqual(sent[6], 0x34, 'deviceId byte 2');
    assertEqual(sent[7], 0x12, 'deviceId byte 3');
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
        // Build HELLO response
        const response = new Uint8Array(12);
        const view = new DataView(response.buffer);
        view.setUint8(0, 0x00);  // HELLO opcode
        view.setUint8(1, 1);     // version
        view.setUint16(2, 2048, true);  // maxPacket
        view.setUint32(4, 0xABCD1234, true);  // sessionId
        view.setUint32(8, 1700000000, true);  // timestamp

        client.ws._simulateMessage(response);

        assertEqual(client.connected, true, 'connected');
        assertEqual(client.sessionId, 0xABCD1234, 'sessionId');
        assertEqual(connectInfo.version, 1, 'version');
        assertEqual(connectInfo.sessionId, 0xABCD1234, 'event sessionId');
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

    const sent = new Uint8Array(client.ws.sentMessages[0]);
    assertEqual(sent[0], 0x01, 'opcode PROPERTY_UPDATE_SHORT');
    assertEqual(sent[1], 3, 'property ID');
    assertEqual(sent[2], 0, 'namespace');
    assertEqual(sent[3], 200, 'value');
});

test('encodes BOOL property update', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;

    client.properties.set(2, { id: 2, name: 'enabled', typeId: 0x01, readonly: false });
    client.propertyByName.set('enabled', 2);

    client.setProperty('enabled', true);

    const sent = new Uint8Array(client.ws.sentMessages[0]);
    assertEqual(sent[0], 0x01, 'opcode');
    assertEqual(sent[1], 2, 'property ID');
    assertEqual(sent[3], 1, 'value true');

    client.setProperty('enabled', false);
    const sent2 = new Uint8Array(client.ws.sentMessages[1]);
    assertEqual(sent2[3], 0, 'value false');
});

test('encodes FLOAT32 property update', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;

    client.properties.set(1, { id: 1, name: 'speed', typeId: 0x05, readonly: false });
    client.propertyByName.set('speed', 1);

    client.setProperty('speed', 2.5);

    const sent = new Uint8Array(client.ws.sentMessages[0]);
    const view = new DataView(sent.buffer);
    assertEqual(sent[0], 0x01, 'opcode');
    assertEqual(sent[1], 1, 'property ID');

    const floatVal = view.getFloat32(3, true);
    assert(Math.abs(floatVal - 2.5) < 0.001, 'float value');
});

test('encodes INT32 property update', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });
    client.ws = new MockWebSocket('ws://test:81');
    client.ws.readyState = MockWebSocket.OPEN;

    client.properties.set(5, { id: 5, name: 'count', typeId: 0x04, readonly: false });
    client.propertyByName.set('count', 5);

    client.setProperty('count', -12345);

    const sent = new Uint8Array(client.ws.sentMessages[0]);
    const view = new DataView(sent.buffer);
    assertEqual(view.getInt32(3, true), -12345, 'int32 value');
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

    // Single property update (no batch flag)
    const msg = new Uint8Array([
        0x01,  // PROPERTY_UPDATE_SHORT opcode
        3,     // property ID
        0,     // namespace
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

    // Batched update: 3 properties
    const msg = new Uint8Array(2 + 6 + 3 + 3);  // header + float + bool + uint8
    const view = new DataView(msg.buffer);

    msg[0] = 0x81;  // PROPERTY_UPDATE_SHORT with batch flag
    msg[1] = 2;     // count - 1 = 2 (so 3 items)

    // Item 1: speed (float)
    msg[2] = 1;     // prop ID
    msg[3] = 0;     // namespace
    view.setFloat32(4, 1.5, true);

    // Item 2: enabled (bool)
    msg[8] = 2;     // prop ID
    msg[9] = 0;     // namespace
    msg[10] = 1;    // true

    // Item 3: brightness (uint8)
    msg[11] = 3;    // prop ID
    msg[12] = 0;    // namespace
    msg[13] = 128;  // value

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
        0               // UI hints
    ]);

    client._handleMessage(msg);

    assertEqual(schemaProp.id, 3, 'id');
    assertEqual(schemaProp.name, 'brightness', 'name');
    assertEqual(schemaProp.typeId, 0x03, 'typeId');
    assertEqual(client.properties.size, 1, 'properties count');
    assertEqual(client.propertyByName.get('brightness'), 3, 'name lookup');
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

    // Receive PONG
    const pong = new Uint8Array([0x09, 0x01, 0x00, 0x00, 0x00]);  // PONG with payload=1
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
    assertEqual(sent[0], 0x08, 'PING opcode');
    assertEqual(sent.length, 5, 'message length');
});

test('getLastPongAge returns correct value', () => {
    const client = new MicroProtoClient('ws://test:81', { reconnect: false });

    assertEqual(client.getLastPongAge(), -1, 'before any pong');

    client._lastPongTime = Date.now() - 1000;  // 1 second ago
    const age = client.getLastPongAge();

    assert(age >= 1000 && age < 1100, 'age ~1000ms');
});

// ============== Summary ==============

console.log('\n' + '='.repeat(40));
console.log(`Tests: ${testCount}, Passed: ${passCount}, Failed: ${failCount}`);
console.log('='.repeat(40));

if (failCount > 0) {
    process.exit(1);
}
