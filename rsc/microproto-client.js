/**
 * MicroProto WebSocket Client
 *
 * Binary protocol client for ESP32 property synchronization.
 *
 * Usage:
 *   const client = new MicroProtoClient('ws://192.168.1.100:81');
 *   client.on('ready', (properties) => console.log('Connected!', properties));
 *   client.on('property', (id, name, value) => updateUI(name, value));
 *   client.connect();
 *
 *   // Update a property
 *   client.setProperty('brightness', 128);
 */

class MicroProtoClient {
    // Protocol constants
    static PROTOCOL_VERSION = 1;
    static OPCODES = {
        HELLO: 0x00,
        PROPERTY_UPDATE_SHORT: 0x01,
        PROPERTY_UPDATE_LONG: 0x02,
        SCHEMA_UPSERT: 0x03,
        SCHEMA_DELETE: 0x04,
        RPC_CALL: 0x05,
        RPC_RESPONSE: 0x06,
        ERROR: 0x07,
        PING: 0x08,
        PONG: 0x09
    };
    static TYPES = {
        BOOL: 0x01,
        INT8: 0x02,
        UINT8: 0x03,
        INT32: 0x04,
        FLOAT32: 0x05,
        STRING: 0x10
    };

    constructor(url, options = {}) {
        this.url = url;
        this.options = {
            deviceId: options.deviceId || Math.floor(Math.random() * 0xFFFFFFFF),
            maxPacketSize: options.maxPacketSize || 4096,
            reconnect: options.reconnect !== false,
            reconnectDelay: options.reconnectDelay || 2000,
            heartbeatInterval: options.heartbeatInterval || 5000,  // Send ping every 5s
            heartbeatTimeout: options.heartbeatTimeout || 10000,   // Connection lost if no pong in 10s
            debug: options.debug || false
        };

        this.ws = null;
        this.connected = false;
        this.sessionId = null;
        this.properties = new Map();  // id -> {name, type, value, readonly, ...}
        this.propertyByName = new Map();  // name -> id
        this._listeners = {};
        this._reconnectTimer = null;
        this._heartbeatTimer = null;
        this._heartbeatTimeoutTimer = null;
        this._lastPongTime = 0;
        this._pingPayload = 0;
    }

    // Event handling
    on(event, callback) {
        if (!this._listeners[event]) {
            this._listeners[event] = [];
        }
        this._listeners[event].push(callback);
        return this;
    }

    off(event, callback) {
        if (this._listeners[event]) {
            this._listeners[event] = this._listeners[event].filter(cb => cb !== callback);
        }
        return this;
    }

    _emit(event, ...args) {
        if (this._listeners[event]) {
            this._listeners[event].forEach(cb => cb(...args));
        }
    }

    _log(...args) {
        if (this.options.debug) {
            console.log('[MicroProto]', ...args);
        }
    }

    // Connection management
    connect() {
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            return;
        }

        this._log('Connecting to', this.url);
        this.ws = new WebSocket(this.url);
        this.ws.binaryType = 'arraybuffer';

        this.ws.onopen = () => {
            this._log('WebSocket connected, sending HELLO');
            this._sendHello();
        };

        this.ws.onmessage = (event) => {
            if (event.data instanceof ArrayBuffer) {
                this._handleMessage(new Uint8Array(event.data));
            }
        };

        this.ws.onerror = (error) => {
            this._log('WebSocket error:', error);
            this._emit('error', error);
        };

        this.ws.onclose = () => {
            this._log('WebSocket closed');
            this.connected = false;
            this._stopHeartbeat();
            this._readyEmitted = false;  // Reset for next connection
            this._emit('disconnect');

            if (this.options.reconnect) {
                this._scheduleReconnect();
            }
        };
    }

    disconnect() {
        this.options.reconnect = false;
        this._stopHeartbeat();
        if (this._reconnectTimer) {
            clearTimeout(this._reconnectTimer);
            this._reconnectTimer = null;
        }
        if (this.ws) {
            this.ws.close();
            this.ws = null;
        }
    }

    _startHeartbeat() {
        this._stopHeartbeat();
        this._lastPongTime = Date.now();

        this._heartbeatTimer = setInterval(() => {
            this._sendPing();
        }, this.options.heartbeatInterval);

        this._log('Heartbeat started (interval:', this.options.heartbeatInterval, 'ms)');
    }

    _stopHeartbeat() {
        if (this._heartbeatTimer) {
            clearInterval(this._heartbeatTimer);
            this._heartbeatTimer = null;
        }
        if (this._heartbeatTimeoutTimer) {
            clearTimeout(this._heartbeatTimeoutTimer);
            this._heartbeatTimeoutTimer = null;
        }
    }

    _sendPing() {
        if (!this.isConnected()) return;

        const payload = this._pingPayload++;

        const buf = new ArrayBuffer(5);
        const view = new DataView(buf);
        view.setUint8(0, MicroProtoClient.OPCODES.PING);
        view.setUint32(1, payload, true);

        this._log('Sending heartbeat PING:', payload);
        this.ws.send(buf);

        // Set timeout for pong response
        if (this._heartbeatTimeoutTimer) {
            clearTimeout(this._heartbeatTimeoutTimer);
        }
        this._heartbeatTimeoutTimer = setTimeout(() => {
            this._onHeartbeatTimeout();
        }, this.options.heartbeatTimeout);
    }

    _onHeartbeatTimeout() {
        this._log('Heartbeat timeout - connection lost');
        this._stopHeartbeat();
        this._emit('connectionLost');

        // Force close and trigger reconnect
        if (this.ws) {
            this.ws.close();
        }
    }

    _scheduleReconnect() {
        if (this._reconnectTimer) return;

        this._log(`Reconnecting in ${this.options.reconnectDelay}ms`);
        this._reconnectTimer = setTimeout(() => {
            this._reconnectTimer = null;
            this.connect();
        }, this.options.reconnectDelay);
    }

    // Protocol encoding
    _sendHello() {
        const buf = new ArrayBuffer(8);
        const view = new DataView(buf);

        view.setUint8(0, MicroProtoClient.OPCODES.HELLO);  // opcode
        view.setUint8(1, MicroProtoClient.PROTOCOL_VERSION);
        view.setUint16(2, this.options.maxPacketSize, true);  // little-endian
        view.setUint32(4, this.options.deviceId, true);

        this.ws.send(buf);
    }

    _sendPropertyUpdate(propertyId, value, typeId) {
        const size = this._getTypeSize(typeId);
        const buf = new ArrayBuffer(4 + size);
        const view = new DataView(buf);
        const bytes = new Uint8Array(buf);

        view.setUint8(0, MicroProtoClient.OPCODES.PROPERTY_UPDATE_SHORT);
        view.setUint8(1, propertyId);
        view.setUint8(2, 0);  // namespace (always 0 for now)

        this._encodeValue(view, 3, value, typeId);

        this._log('Sending property update:', propertyId, '=', value);
        this.ws.send(buf);
    }

    // Protocol decoding
    _handleMessage(data) {
        if (data.length === 0) return;

        const header = data[0];
        const opcode = header & 0x0F;
        const flags = (header >> 4) & 0x07;
        const batch = (header >> 7) & 0x01;

        this._log('Received opcode:', opcode, 'flags:', flags, 'batch:', batch);

        switch (opcode) {
            case MicroProtoClient.OPCODES.HELLO:
                this._handleHelloResponse(data);
                break;
            case MicroProtoClient.OPCODES.SCHEMA_UPSERT:
                this._handleSchemaUpsert(data, batch);
                break;
            case MicroProtoClient.OPCODES.PROPERTY_UPDATE_SHORT:
                this._handlePropertyUpdate(data, batch);
                break;
            case MicroProtoClient.OPCODES.ERROR:
                this._handleError(data);
                break;
            case MicroProtoClient.OPCODES.PONG:
                this._handlePong(data);
                break;
            default:
                this._log('Unknown opcode:', opcode);
        }
    }

    _handleHelloResponse(data) {
        if (data.length < 12) {
            this._log('Invalid HELLO response');
            return;
        }

        const view = new DataView(data.buffer, data.byteOffset);
        const version = view.getUint8(1);
        const maxPacket = view.getUint16(2, true);
        this.sessionId = view.getUint32(4, true);
        const timestamp = view.getUint32(8, true);

        this._log('HELLO response: version=', version, 'session=', this.sessionId);
        this.connected = true;
        this._emit('connect', { version, maxPacket, sessionId: this.sessionId, timestamp });
    }

    _handleSchemaUpsert(data, batch) {
        let offset = 1;
        let count = 1;

        if (batch) {
            count = data[offset] + 1;
            offset++;
        }

        this._log('Schema upsert:', count, 'properties');

        for (let i = 0; i < count && offset < data.length; i++) {
            const result = this._decodeSchemaItem(data, offset);
            if (!result) break;

            const [prop, newOffset] = result;
            offset = newOffset;

            this.properties.set(prop.id, prop);
            this.propertyByName.set(prop.name, prop.id);

            this._log('  Property:', prop.name, 'id=', prop.id, 'type=', prop.typeId);
            this._emit('schema', prop);
        }
    }

    _decodeSchemaItem(data, offset) {
        if (offset >= data.length) return null;

        const itemType = data[offset++];
        const propType = itemType & 0x0F;
        const readonly = !!(itemType & 0x10);
        const persistent = !!(itemType & 0x20);
        const hidden = !!(itemType & 0x40);

        if (propType !== 1) {  // PROPERTY
            return null;
        }

        // Property level flags
        const levelFlags = data[offset++];
        const level = levelFlags & 0x03;
        const bleExposed = !!(levelFlags & 0x04);

        // Group ID if GROUP level
        let groupId = 0;
        if (level === 1) {
            groupId = data[offset++];
        }

        // Item ID
        const id = data[offset++];

        // Namespace ID
        const namespaceId = data[offset++];

        // Name
        const nameLen = data[offset++];
        const name = new TextDecoder().decode(data.slice(offset, offset + nameLen));
        offset += nameLen;

        // Description (varint length)
        const [descLen, varintBytes] = this._decodeVarint(data, offset);
        offset += varintBytes;
        offset += descLen;  // Skip description

        // Type ID
        const typeId = data[offset++];

        // Validation flags (skip)
        offset++;

        // Default value
        const valueSize = this._getTypeSize(typeId);
        offset += valueSize;

        // UI hints (skip)
        offset++;

        return [{
            id,
            name,
            typeId,
            readonly,
            persistent,
            hidden,
            level,
            groupId,
            namespaceId,
            bleExposed,
            value: null
        }, offset];
    }

    _handlePropertyUpdate(data, batch) {
        let offset = 1;
        let count = 1;

        if (batch) {
            count = data[offset] + 1;
            offset++;
        }

        this._log('Property update:', count, 'values');

        const view = new DataView(data.buffer, data.byteOffset);

        for (let i = 0; i < count && offset < data.length; i++) {
            const propId = data[offset++];
            const namespaceId = data[offset++];

            const prop = this.properties.get(propId);
            if (!prop) {
                this._log('Unknown property:', propId);
                continue;
            }

            const [value, bytesRead] = this._decodeValue(view, offset, prop.typeId);
            offset += bytesRead;

            const oldValue = prop.value;
            prop.value = value;

            this._log('  ', prop.name, '=', value);
            this._emit('property', propId, prop.name, value, oldValue);
        }

        // Emit ready once we have all initial values
        if (!this._readyEmitted && this.properties.size > 0) {
            this._readyEmitted = true;
            this._emit('ready', this.getProperties());
            // Start heartbeat after initial sync complete
            this._startHeartbeat();
        }
    }

    _handleError(data) {
        const view = new DataView(data.buffer, data.byteOffset);

        const errorCode = view.getUint16(1, true);
        const [msgLen, varintBytes] = this._decodeVarint(data, 3);
        const message = new TextDecoder().decode(data.slice(3 + varintBytes, 3 + varintBytes + msgLen));

        this._log('Error:', errorCode, message);
        this._emit('error', { code: errorCode, message });
    }

    _handlePong(data) {
        // Clear heartbeat timeout - connection is alive
        if (this._heartbeatTimeoutTimer) {
            clearTimeout(this._heartbeatTimeoutTimer);
            this._heartbeatTimeoutTimer = null;
        }
        this._lastPongTime = Date.now();
        this._log('PONG received - connection alive');
    }

    // Value encoding/decoding
    _encodeValue(view, offset, value, typeId) {
        switch (typeId) {
            case MicroProtoClient.TYPES.BOOL:
                view.setUint8(offset, value ? 1 : 0);
                break;
            case MicroProtoClient.TYPES.INT8:
                view.setInt8(offset, value);
                break;
            case MicroProtoClient.TYPES.UINT8:
                view.setUint8(offset, value);
                break;
            case MicroProtoClient.TYPES.INT32:
                view.setInt32(offset, value, true);
                break;
            case MicroProtoClient.TYPES.FLOAT32:
                view.setFloat32(offset, value, true);
                break;
        }
    }

    _decodeValue(view, offset, typeId) {
        switch (typeId) {
            case MicroProtoClient.TYPES.BOOL:
                return [!!view.getUint8(offset), 1];
            case MicroProtoClient.TYPES.INT8:
                return [view.getInt8(offset), 1];
            case MicroProtoClient.TYPES.UINT8:
                return [view.getUint8(offset), 1];
            case MicroProtoClient.TYPES.INT32:
                return [view.getInt32(offset, true), 4];
            case MicroProtoClient.TYPES.FLOAT32:
                return [view.getFloat32(offset, true), 4];
            default:
                return [null, 0];
        }
    }

    _getTypeSize(typeId) {
        switch (typeId) {
            case MicroProtoClient.TYPES.BOOL:
            case MicroProtoClient.TYPES.INT8:
            case MicroProtoClient.TYPES.UINT8:
                return 1;
            case MicroProtoClient.TYPES.INT32:
            case MicroProtoClient.TYPES.FLOAT32:
                return 4;
            default:
                return 0;
        }
    }

    _decodeVarint(data, offset) {
        let result = 0;
        let shift = 0;
        let bytesConsumed = 0;

        while (offset < data.length) {
            const byte = data[offset++];
            bytesConsumed++;
            result |= (byte & 0x7F) << shift;
            if ((byte & 0x80) === 0) break;
            shift += 7;
            if (bytesConsumed > 5) break;
        }

        return [result, bytesConsumed];
    }

    // Public API
    setProperty(nameOrId, value) {
        let prop;

        if (typeof nameOrId === 'string') {
            const id = this.propertyByName.get(nameOrId);
            if (id === undefined) {
                console.warn('Unknown property:', nameOrId);
                return false;
            }
            prop = this.properties.get(id);
        } else {
            prop = this.properties.get(nameOrId);
        }

        if (!prop) {
            console.warn('Property not found:', nameOrId);
            return false;
        }

        if (prop.readonly) {
            console.warn('Property is readonly:', prop.name);
            return false;
        }

        this._sendPropertyUpdate(prop.id, value, prop.typeId);
        return true;
    }

    getProperty(nameOrId) {
        let prop;

        if (typeof nameOrId === 'string') {
            const id = this.propertyByName.get(nameOrId);
            prop = this.properties.get(id);
        } else {
            prop = this.properties.get(nameOrId);
        }

        return prop ? prop.value : undefined;
    }

    getProperties() {
        const result = {};
        for (const [id, prop] of this.properties) {
            result[prop.name] = {
                id: prop.id,
                value: prop.value,
                type: prop.typeId,
                readonly: prop.readonly
            };
        }
        return result;
    }

    isConnected() {
        return this.connected && this.ws && this.ws.readyState === WebSocket.OPEN;
    }

    /**
     * Get time since last successful heartbeat pong
     * @returns {number} Milliseconds since last pong, or -1 if never received
     */
    getLastPongAge() {
        if (this._lastPongTime === 0) return -1;
        return Date.now() - this._lastPongTime;
    }
}

// Export for module systems
if (typeof module !== 'undefined' && module.exports) {
    module.exports = MicroProtoClient;
}
