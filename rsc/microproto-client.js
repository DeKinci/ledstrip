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
        STRING: 0x10,
        ARRAY: 0x20,
        LIST: 0x21,
        OBJECT: 0x22
    };
    // Pastel color palette (matches UIColor enum in PropertyBase.h)
    static COLORS = {
        0: null,              // NONE
        1: '#fda4af',         // ROSE
        2: '#fcd34d',         // AMBER
        3: '#bef264',         // LIME
        4: '#67e8f9',         // CYAN
        5: '#c4b5fd',         // VIOLET
        6: '#f9a8d4',         // PINK
        7: '#5eead4',         // TEAL
        8: '#fdba74',         // ORANGE
        9: '#7dd3fc',         // SKY
        10: '#a5b4fc',        // INDIGO
        11: '#6ee7b7',        // EMERALD
        12: '#cbd5e1'         // SLATE
    };
    static COLOR_NAMES = {
        0: null, 1: 'rose', 2: 'amber', 3: 'lime', 4: 'cyan', 5: 'violet',
        6: 'pink', 7: 'teal', 8: 'orange', 9: 'sky', 10: 'indigo', 11: 'emerald', 12: 'slate'
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

        // Description (varint length + string)
        const [descLen, varintBytes] = this._decodeVarint(data, offset);
        offset += varintBytes;
        const description = descLen > 0
            ? new TextDecoder().decode(data.slice(offset, offset + descLen))
            : null;
        offset += descLen;

        // DATA_TYPE_DEFINITION - Type ID
        const typeId = data[offset++];

        // Container type info and constraints
        let elementCount = 0;
        let elementTypeId = 0;
        let constraints = {};
        let elementConstraints = {};
        let lengthConstraints = {};

        if (typeId === MicroProtoClient.TYPES.ARRAY) {
            // ARRAY: varint(element_count) + element DATA_TYPE_DEFINITION
            const [count, countBytes] = this._decodeVarint(data, offset);
            elementCount = count;
            offset += countBytes;
            // Element DATA_TYPE_DEFINITION: element_type_id + element_validation_flags [+ min/max/step]
            elementTypeId = data[offset++];
            const result = this._parseValidationConstraints(data, offset, elementTypeId);
            elementConstraints = result.constraints;
            offset = result.newOffset;
        } else if (typeId === MicroProtoClient.TYPES.LIST) {
            // LIST: length_constraints + element DATA_TYPE_DEFINITION
            const lenResult = this._parseLengthConstraints(data, offset);
            lengthConstraints = lenResult.lengthConstraints;
            offset = lenResult.newOffset;
            // Element DATA_TYPE_DEFINITION: element_type_id + element_validation_flags [+ min/max/step]
            elementTypeId = data[offset++];
            const elemResult = this._parseValidationConstraints(data, offset, elementTypeId);
            elementConstraints = elemResult.constraints;
            offset = elemResult.newOffset;
        } else {
            // Basic type: validation_flags [+ min/max/step values]
            const result = this._parseValidationConstraints(data, offset, typeId);
            constraints = result.constraints;
            offset = result.newOffset;
        }

        // Default value - skip based on type
        const defaultValueSize = this._getDefaultValueSize(typeId, elementTypeId, elementCount, data, offset);
        offset += defaultValueSize;

        // Parse UI hints
        const uiResult = this._parseUIHints(data, offset);
        const ui = uiResult.ui;
        offset = uiResult.newOffset;

        return [{
            id,
            name,
            description,           // Human-readable description (or null)
            typeId,
            elementTypeId,
            elementCount,
            readonly,
            persistent,
            hidden,
            level,
            groupId,
            namespaceId,
            bleExposed,
            constraints,           // For basic types: { min, max, step }
            elementConstraints,    // For ARRAY/LIST: element constraints
            lengthConstraints,     // For LIST: { minLength, maxLength }
            ui,                    // UI hints: { color, colorHex, unit, icon, widget }
            value: null
        }, offset];
    }

    _getDefaultValueSize(typeId, elementTypeId, elementCount, data, offset) {
        if (typeId === MicroProtoClient.TYPES.ARRAY) {
            // ARRAY: fixed count * element size
            return elementCount * this._getBasicTypeSize(elementTypeId);
        } else if (typeId === MicroProtoClient.TYPES.LIST) {
            // LIST: varint(count) + count * element size
            const [count, countBytes] = this._decodeVarint(data, offset);
            return countBytes + count * this._getBasicTypeSize(elementTypeId);
        } else {
            return this._getBasicTypeSize(typeId);
        }
    }

    _getBasicTypeSize(typeId) {
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

    // Parse validation constraints: flags byte + optional min/max/step values
    // Returns: { constraints: {...}, newOffset }
    _parseValidationConstraints(data, offset, typeId) {
        const view = new DataView(data.buffer, data.byteOffset);
        const constraints = { hasMin: false, hasMax: false, hasStep: false };

        if (offset >= data.length) return { constraints, newOffset: offset };

        const flags = data[offset++];
        const typeSize = this._getBasicTypeSize(typeId);

        // flags: bit 0 = hasMin, bit 1 = hasMax, bit 2 = hasStep
        if (flags & 0x01) {
            constraints.hasMin = true;
            const [val] = this._decodeValue(view, offset, typeId);
            constraints.min = val;
            offset += typeSize;
        }
        if (flags & 0x02) {
            constraints.hasMax = true;
            const [val] = this._decodeValue(view, offset, typeId);
            constraints.max = val;
            offset += typeSize;
        }
        if (flags & 0x04) {
            constraints.hasStep = true;
            const [val] = this._decodeValue(view, offset, typeId);
            constraints.step = val;
            offset += typeSize;
        }

        return { constraints, newOffset: offset };
    }

    // Parse length constraints for LIST: flags byte + optional minLength/maxLength varints
    // Returns: { lengthConstraints: {...}, newOffset }
    _parseLengthConstraints(data, offset) {
        const lengthConstraints = { hasMinLength: false, hasMaxLength: false };

        if (offset >= data.length) return { lengthConstraints, newOffset: offset };

        const flags = data[offset++];

        // flags: bit 0 = hasMinLength, bit 1 = hasMaxLength
        if (flags & 0x01) {
            lengthConstraints.hasMinLength = true;
            const [val, bytes] = this._decodeVarint(data, offset);
            lengthConstraints.minLength = val;
            offset += bytes;
        }
        if (flags & 0x02) {
            lengthConstraints.hasMaxLength = true;
            const [val, bytes] = this._decodeVarint(data, offset);
            lengthConstraints.maxLength = val;
            offset += bytes;
        }

        return { lengthConstraints, newOffset: offset };
    }

    // Parse UI hints: flags byte with colorgroup in upper 4 bits
    // Wire format: flags (bit0:widget, bit1:unit, bit2:icon, bit3:reserved, bits4-7:colorgroup)
    //              + [widget: u8] + [unit_len: u8, unit: bytes] + [icon_len: u8, icon: bytes]
    // Returns: { ui: {...}, newOffset }
    _parseUIHints(data, offset) {
        const ui = {
            color: null,       // Color name (e.g., 'rose', 'amber')
            colorHex: null,    // Hex color code (e.g., '#fda4af')
            unit: null,        // Unit label (e.g., 'ms', '%', '°C')
            icon: null,        // Emoji icon (e.g., '💡', '🎨')
            widget: 0          // Widget hint (0=auto, meaning depends on type)
        };

        if (offset >= data.length) return { ui, newOffset: offset };

        const flags = data[offset++];
        const hasWidget = !!(flags & 0x01);
        const hasUnit = !!(flags & 0x02);
        const hasIcon = !!(flags & 0x04);
        const colorGroup = (flags >> 4) & 0x0F;  // Upper 4 bits

        // Color from colorgroup (embedded in flags byte)
        if (colorGroup > 0) {
            ui.color = MicroProtoClient.COLOR_NAMES[colorGroup] || null;
            ui.colorHex = MicroProtoClient.COLORS[colorGroup] || null;
        }

        // Widget hint (first per spec order)
        if (hasWidget && offset < data.length) {
            ui.widget = data[offset++];
        }

        // Unit string
        if (hasUnit && offset < data.length) {
            const unitLen = data[offset++];
            if (unitLen > 0 && offset + unitLen <= data.length) {
                ui.unit = new TextDecoder().decode(data.slice(offset, offset + unitLen));
                offset += unitLen;
            }
        }

        // Icon string (emoji)
        if (hasIcon && offset < data.length) {
            const iconLen = data[offset++];
            if (iconLen > 0 && offset + iconLen <= data.length) {
                ui.icon = new TextDecoder().decode(data.slice(offset, offset + iconLen));
                offset += iconLen;
            }
        }

        return { ui, newOffset: offset };
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

            const [value, bytesRead] = this._decodePropertyValue(view, data, offset, prop);
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

    _decodePropertyValue(view, data, offset, prop) {
        const typeId = prop.typeId;

        if (typeId === MicroProtoClient.TYPES.ARRAY) {
            // ARRAY: fixed count elements (no length prefix)
            const count = prop.elementCount;
            const elementTypeId = prop.elementTypeId;
            const elementSize = this._getBasicTypeSize(elementTypeId);
            const values = [];
            let bytesRead = 0;

            for (let i = 0; i < count; i++) {
                const [val, size] = this._decodeValue(view, offset + bytesRead, elementTypeId);
                values.push(val);
                bytesRead += size;
            }
            return [values, bytesRead];

        } else if (typeId === MicroProtoClient.TYPES.LIST) {
            // LIST: varint(count) + elements
            const [count, countBytes] = this._decodeVarint(data, offset);
            const elementTypeId = prop.elementTypeId;
            const values = [];
            let bytesRead = countBytes;

            for (let i = 0; i < count; i++) {
                const [val, size] = this._decodeValue(view, offset + bytesRead, elementTypeId);
                values.push(val);
                bytesRead += size;
            }
            return [values, bytesRead];

        } else {
            // Basic type
            return this._decodeValue(view, offset, typeId);
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
