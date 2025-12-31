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
    // Opcodes from MVP spec (lib/microproto/wire/OpCode.h)
    static OPCODES = {
        HELLO: 0x00,           // Protocol handshake
        PROPERTY_UPDATE: 0x01, // Property value update (unified, flags: batch, has_timestamp)
        // 0x02 reserved for PROPERTY_DELTA
        SCHEMA_UPSERT: 0x03,   // Create or update schema
        SCHEMA_DELETE: 0x04,   // Delete schema definition
        RPC: 0x05,             // Remote procedure call (flags: is_response, needs_response/success)
        PING: 0x06,            // Heartbeat (flags: is_response)
        ERROR: 0x07,           // Error message
        RESOURCE_GET: 0x08,    // Get resource body
        RESOURCE_PUT: 0x09,    // Create/update resource
        RESOURCE_DELETE: 0x0A  // Delete resource
    };
    static TYPES = {
        // Basic types (Section 3.1)
        BOOL: 0x01,
        INT8: 0x02,
        UINT8: 0x03,
        INT32: 0x04,
        FLOAT32: 0x05,
        // 0x06-0x1F reserved for future basic types
        // Composite types (Section 3.2)
        ARRAY: 0x20,
        LIST: 0x21,
        OBJECT: 0x22,
        VARIANT: 0x23,
        RESOURCE: 0x24
        // 0x25-0x2F reserved for future composite types
    };

    // Error codes (Section 8.1)
    static ERROR_CODES = {
        SUCCESS: 0x0000,
        INVALID_OPCODE: 0x0001,
        INVALID_PROPERTY_ID: 0x0002,
        INVALID_FUNCTION_ID: 0x0003,
        TYPE_MISMATCH: 0x0004,
        VALIDATION_FAILED: 0x0005,
        OUT_OF_RANGE: 0x0006,
        PERMISSION_DENIED: 0x0007,
        NOT_IMPLEMENTED: 0x0008,
        PROTOCOL_VERSION_MISMATCH: 0x0009,
        BUFFER_OVERFLOW: 0x000A
    };

    // Widget hints for UI generation
    static WIDGETS = {
        AUTO: 0,
        SLIDER: 1,
        TOGGLE: 2,
        COLOR_PICKER: 3,
        TEXT_INPUT: 4,
        DROPDOWN: 5,
        NUMBER_INPUT: 6
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
        this.functions = new Map();  // id -> {name, params, returnType, ...}
        this.functionByName = new Map();  // name -> id
        this._listeners = {};
        this._reconnectTimer = null;
        this._heartbeatTimer = null;
        this._heartbeatTimeoutTimer = null;
        this._lastPongTime = 0;
        this._pingPayload = 0;
        // RPC tracking
        this._rpcCallId = 0;
        this._pendingRpcCalls = new Map();  // callId -> {resolve, reject, timeout}
        this._rpcTimeout = options.rpcTimeout || 60000;  // 60s per spec
        // Resource operations tracking
        this._resourceRequestId = 0;
        this._pendingResourceOps = new Map();  // requestId -> {resolve, reject, timeout}
        this._resourceTimeout = options.resourceTimeout || 30000;  // 30s for resource ops
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
        // PING request: opcode=0x6, flags=0 (is_response=0)
        view.setUint8(0, MicroProtoClient.OPCODES.PING);
        view.setUint32(1, payload, true);

        this.ws.send(buf);

        // Set timeout for pong response
        if (this._heartbeatTimeoutTimer) {
            clearTimeout(this._heartbeatTimeoutTimer);
        }
        this._heartbeatTimeoutTimer = setTimeout(() => {
            this._onHeartbeatTimeout();
        }, this.options.heartbeatTimeout);
    }

    _sendPong(data) {
        if (!this.isConnected()) return;

        // Echo back the payload with is_response flag set
        const buf = new ArrayBuffer(data.length);
        const bytes = new Uint8Array(buf);
        bytes.set(data);
        // Set is_response flag (bit0 in flags = bit4 in header byte)
        bytes[0] = MicroProtoClient.OPCODES.PING | 0x10;  // opcode | (flags << 4)

        this.ws.send(buf);
        this._log('Sent PONG response');
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
        // HELLO request format (MVP spec):
        // u8 operation_header { opcode: 0x0, flags: 0x0 }
        // u8 protocol_version
        // varint max_packet_size
        // varint device_id
        const maxPacketSizeBytes = this._encodeVarint(this.options.maxPacketSize);
        const deviceIdBytes = this._encodeVarint(this.options.deviceId);

        const buf = new ArrayBuffer(2 + maxPacketSizeBytes.length + deviceIdBytes.length);
        const bytes = new Uint8Array(buf);

        bytes[0] = MicroProtoClient.OPCODES.HELLO;  // opcode=0, flags=0 (request)
        bytes[1] = MicroProtoClient.PROTOCOL_VERSION;
        bytes.set(maxPacketSizeBytes, 2);
        bytes.set(deviceIdBytes, 2 + maxPacketSizeBytes.length);

        this.ws.send(buf);
    }

    _sendPropertyUpdate(propertyId, value, typeId) {
        const size = this._getTypeSize(typeId);
        // MVP format: opheader(1) + propid(1-2) + value
        const propIdSize = propertyId < 128 ? 1 : 2;
        const buf = new ArrayBuffer(1 + propIdSize + size);
        const view = new DataView(buf);

        // Opheader: opcode=1 (PROPERTY_UPDATE), flags=0 (single, no timestamp)
        view.setUint8(0, MicroProtoClient.OPCODES.PROPERTY_UPDATE);

        // PropId encoding (1 byte for 0-127, 2 bytes for 128+)
        let offset = 1;
        if (propertyId < 128) {
            view.setUint8(offset++, propertyId);
        } else {
            view.setUint8(offset++, 0x80 | (propertyId & 0x7F));
            view.setUint8(offset++, propertyId >> 7);
        }

        this._encodeValue(view, offset, value, typeId);

        this._log('Sending property update:', propertyId, '=', value);
        this.ws.send(buf);
    }

    // Protocol decoding
    _handleMessage(data) {
        if (data.length === 0) return;

        const header = data[0];
        const opcode = header & 0x0F;       // bits 0-3
        const flags = (header >> 4) & 0x0F; // bits 4-7

        this._log('Received opcode:', opcode, 'flags:', flags);

        switch (opcode) {
            case MicroProtoClient.OPCODES.HELLO:
                this._handleHelloResponse(data, flags);
                break;
            case MicroProtoClient.OPCODES.SCHEMA_UPSERT:
                this._handleSchemaUpsert(data, flags);
                break;
            case MicroProtoClient.OPCODES.SCHEMA_DELETE:
                this._handleSchemaDelete(data, flags);
                break;
            case MicroProtoClient.OPCODES.PROPERTY_UPDATE:
                this._handlePropertyUpdate(data, flags);
                break;
            case MicroProtoClient.OPCODES.RPC:
                this._handleRpc(data, flags);
                break;
            case MicroProtoClient.OPCODES.ERROR:
                this._handleError(data, flags);
                break;
            case MicroProtoClient.OPCODES.PING:
                // PING with is_response flag = PONG
                if (flags & 0x01) {
                    this._handlePong(data);
                } else {
                    // Received ping request - send pong response
                    this._sendPong(data);
                }
                break;
            case MicroProtoClient.OPCODES.RESOURCE_GET:
                this._handleResourceGetResponse(data, flags);
                break;
            case MicroProtoClient.OPCODES.RESOURCE_PUT:
                this._handleResourcePutResponse(data, flags);
                break;
            case MicroProtoClient.OPCODES.RESOURCE_DELETE:
                this._handleResourceDeleteResponse(data, flags);
                break;
            default:
                this._log('Unknown opcode:', opcode);
        }
    }

    _handleHelloResponse(data, flags) {
        // HELLO response format (MVP spec):
        // u8 operation_header { opcode: 0x0, flags: 0x1 (is_response) }
        // u8 protocol_version
        // varint max_packet_size
        // varint session_id
        // varint server_timestamp
        if (data.length < 4) {
            this._log('Invalid HELLO response: too short');
            return;
        }

        let offset = 1;
        const version = data[offset++];

        const [maxPacket, mpBytes] = this._decodeVarint(data, offset);
        offset += mpBytes;

        const [sessionId, sidBytes] = this._decodeVarint(data, offset);
        offset += sidBytes;

        const [timestamp, tsBytes] = this._decodeVarint(data, offset);
        offset += tsBytes;

        this.sessionId = sessionId;

        // Check protocol version
        if (version !== MicroProtoClient.PROTOCOL_VERSION) {
            this._log('Protocol version mismatch: expected', MicroProtoClient.PROTOCOL_VERSION, 'got', version);
            this._emit('error', { code: MicroProtoClient.ERROR_CODES.PROTOCOL_VERSION_MISMATCH, message: 'Protocol version mismatch' });
            return;
        }

        this._log('HELLO response: version=', version, 'session=', sessionId, 'timestamp=', timestamp);
        this.connected = true;

        // Clear any existing state for resync
        this.properties.clear();
        this.propertyByName.clear();
        this.functions.clear();
        this.functionByName.clear();

        this._emit('connect', { version, maxPacket, sessionId, timestamp });
    }

    _handleSchemaUpsert(data, flags) {
        // MVP flags: bit0=batch
        const isBatch = !!(flags & 0x01);

        let offset = 1;
        let count = 1;

        if (isBatch) {
            count = data[offset] + 1;  // Stored as count-1
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

        // DATA_TYPE_DEFINITION - Recursive type decoding
        const typeResult = this._decodeDataTypeDefinition(data, offset);
        if (!typeResult) return null;
        const [typeDef, typeOffset] = typeResult;
        offset = typeOffset;

        // Default value - skip based on type
        const defaultValueSize = this._getDefaultValueSize(typeDef, data, offset);
        offset += defaultValueSize;

        // Parse UI hints
        const uiResult = this._parseUIHints(data, offset);
        const ui = uiResult.ui;
        offset = uiResult.newOffset;

        return [{
            id,
            name,
            description,           // Human-readable description (or null)
            typeId: typeDef.typeId,
            // Basic type properties
            constraints: typeDef.constraints || {},
            // Container type properties
            elementTypeId: typeDef.elementTypeId,
            elementCount: typeDef.elementCount,
            elementTypeDef: typeDef.elementTypeDef,  // Nested type def for elements
            elementConstraints: typeDef.elementConstraints || {},
            lengthConstraints: typeDef.lengthConstraints || {},
            // OBJECT fields
            fields: typeDef.fields,  // Array of {name, typeDef}
            // VARIANT options
            variants: typeDef.variants,  // Array of {name, typeDef}
            // RESOURCE types
            headerTypeDef: typeDef.headerTypeDef,
            bodyTypeDef: typeDef.bodyTypeDef,
            // Flags
            readonly,
            persistent,
            hidden,
            level,
            groupId,
            namespaceId,
            bleExposed,
            ui,                    // UI hints: { color, colorHex, unit, icon, widget }
            value: null
        }, offset];
    }

    _getDefaultValueSize(typeDef, data, offset) {
        return this._getValueSize(typeDef, data, offset);
    }

    // Calculate the wire size of a value based on its type definition
    _getValueSize(typeDef, data, offset) {
        const typeId = typeDef.typeId;

        if (typeId === MicroProtoClient.TYPES.ARRAY) {
            // ARRAY: fixed count * element size
            let size = 0;
            for (let i = 0; i < typeDef.elementCount; i++) {
                size += this._getValueSize(typeDef.elementTypeDef, data, offset + size);
            }
            return size;
        } else if (typeId === MicroProtoClient.TYPES.LIST) {
            // LIST: varint(count) + count * element size
            const [count, countBytes] = this._decodeVarint(data, offset);
            let size = countBytes;
            for (let i = 0; i < count; i++) {
                size += this._getValueSize(typeDef.elementTypeDef, data, offset + size);
            }
            return size;
        } else if (typeId === MicroProtoClient.TYPES.OBJECT) {
            // OBJECT: concatenated field values in schema order
            let size = 0;
            for (const field of typeDef.fields) {
                size += this._getValueSize(field.typeDef, data, offset + size);
            }
            return size;
        } else if (typeId === MicroProtoClient.TYPES.VARIANT) {
            // VARIANT: u8 type_index + value of selected type
            const typeIndex = data[offset];
            const selectedVariant = typeDef.variants[typeIndex];
            return 1 + (selectedVariant ? this._getValueSize(selectedVariant.typeDef, data, offset + 1) : 0);
        } else if (typeId === MicroProtoClient.TYPES.RESOURCE) {
            // RESOURCE property updates contain header list only
            // varint(count) + for each: varint(id) + varint(version) + varint(size) + header_value
            const [count, countBytes] = this._decodeVarint(data, offset);
            let size = countBytes;
            for (let i = 0; i < count; i++) {
                const [id, idBytes] = this._decodeVarint(data, offset + size);
                size += idBytes;
                const [version, verBytes] = this._decodeVarint(data, offset + size);
                size += verBytes;
                const [bodySize, sizeBytes] = this._decodeVarint(data, offset + size);
                size += sizeBytes;
                size += this._getValueSize(typeDef.headerTypeDef, data, offset + size);
            }
            return size;
        } else {
            // Basic type
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

    /**
     * Decode DATA_TYPE_DEFINITION (recursive)
     * Returns: [typeDef, newOffset] where typeDef contains:
     *   - typeId: the type ID
     *   - constraints: for basic types {min, max, step, oneof, pattern}
     *   - elementCount, elementTypeDef: for ARRAY
     *   - lengthConstraints, elementTypeDef: for LIST
     *   - fields: for OBJECT [{name, typeDef}, ...]
     *   - variants: for VARIANT [{name, typeDef}, ...]
     *   - headerTypeDef, bodyTypeDef: for RESOURCE
     */
    _decodeDataTypeDefinition(data, offset) {
        if (offset >= data.length) return null;

        const typeId = data[offset++];
        const typeDef = { typeId };

        if (typeId >= 0x01 && typeId <= 0x05) {
            // Basic types: BOOL, INT8, UINT8, INT32, FLOAT32
            const result = this._parseValidationConstraints(data, offset, typeId);
            typeDef.constraints = result.constraints;
            offset = result.newOffset;

        } else if (typeId === MicroProtoClient.TYPES.ARRAY) {
            // ARRAY: varint(element_count) + element DATA_TYPE_DEFINITION
            const [count, countBytes] = this._decodeVarint(data, offset);
            typeDef.elementCount = count;
            offset += countBytes;

            const elemResult = this._decodeDataTypeDefinition(data, offset);
            if (!elemResult) return null;
            typeDef.elementTypeDef = elemResult[0];
            typeDef.elementTypeId = elemResult[0].typeId;  // Convenience field
            typeDef.elementConstraints = elemResult[0].constraints || {};
            offset = elemResult[1];

        } else if (typeId === MicroProtoClient.TYPES.LIST) {
            // LIST: length_constraints + element DATA_TYPE_DEFINITION
            const lenResult = this._parseLengthConstraints(data, offset);
            typeDef.lengthConstraints = lenResult.lengthConstraints;
            offset = lenResult.newOffset;

            const elemResult = this._decodeDataTypeDefinition(data, offset);
            if (!elemResult) return null;
            typeDef.elementTypeDef = elemResult[0];
            typeDef.elementTypeId = elemResult[0].typeId;  // Convenience field
            typeDef.elementConstraints = elemResult[0].constraints || {};
            offset = elemResult[1];

        } else if (typeId === MicroProtoClient.TYPES.OBJECT) {
            // OBJECT: varint(field_count) + for each field: ident(name) + DATA_TYPE_DEFINITION
            const [fieldCount, fcBytes] = this._decodeVarint(data, offset);
            offset += fcBytes;

            typeDef.fields = [];
            for (let i = 0; i < fieldCount; i++) {
                // Field name (ident: u8 length + ASCII bytes)
                const nameLen = data[offset++];
                const fieldName = new TextDecoder().decode(data.slice(offset, offset + nameLen));
                offset += nameLen;

                // Field type definition (recursive)
                const fieldTypeResult = this._decodeDataTypeDefinition(data, offset);
                if (!fieldTypeResult) return null;
                offset = fieldTypeResult[1];

                typeDef.fields.push({ name: fieldName, typeDef: fieldTypeResult[0] });
            }

        } else if (typeId === MicroProtoClient.TYPES.VARIANT) {
            // VARIANT: varint(type_count) + for each: utf8(name) + DATA_TYPE_DEFINITION
            const [typeCount, tcBytes] = this._decodeVarint(data, offset);
            offset += tcBytes;

            typeDef.variants = [];
            for (let i = 0; i < typeCount; i++) {
                // Variant name (utf8: varint length + UTF-8 bytes)
                const [nameLen, nlBytes] = this._decodeVarint(data, offset);
                offset += nlBytes;
                const variantName = new TextDecoder().decode(data.slice(offset, offset + nameLen));
                offset += nameLen;

                // Variant type definition (recursive)
                const variantTypeResult = this._decodeDataTypeDefinition(data, offset);
                if (!variantTypeResult) return null;
                offset = variantTypeResult[1];

                typeDef.variants.push({ name: variantName, typeDef: variantTypeResult[0] });
            }

        } else if (typeId === MicroProtoClient.TYPES.RESOURCE) {
            // RESOURCE: header DATA_TYPE_DEFINITION + body DATA_TYPE_DEFINITION
            const headerResult = this._decodeDataTypeDefinition(data, offset);
            if (!headerResult) return null;
            typeDef.headerTypeDef = headerResult[0];
            offset = headerResult[1];

            const bodyResult = this._decodeDataTypeDefinition(data, offset);
            if (!bodyResult) return null;
            typeDef.bodyTypeDef = bodyResult[0];
            offset = bodyResult[1];
        }

        return [typeDef, offset];
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

    _handlePropertyUpdate(data, flags) {
        // MVP flags: bit0=batch, bit1=has_timestamp
        const isBatch = !!(flags & 0x01);
        const hasTimestamp = !!(flags & 0x02);

        let offset = 1;
        let count = 1;
        let timestamp = 0;

        if (isBatch) {
            count = data[offset] + 1;  // Stored as count-1
            offset++;
        }

        if (hasTimestamp) {
            const [ts, varintBytes] = this._decodeVarint(data, offset);
            timestamp = ts;
            offset += varintBytes;
        }

        this._log('Property update:', count, 'values', hasTimestamp ? `ts=${timestamp}` : '');

        const view = new DataView(data.buffer, data.byteOffset);

        for (let i = 0; i < count && offset < data.length; i++) {
            // PropId encoding: 1 byte for 0-127, 2 bytes for 128+
            let propId = data[offset++];
            if (propId & 0x80) {
                propId = (propId & 0x7F) | (data[offset++] << 7);
            }

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
        // Use the property's typeDef if available, otherwise create a basic one
        const typeDef = prop.elementTypeDef ? prop : { typeId: prop.typeId, ...prop };
        return this._decodeTypedValue(view, data, offset, typeDef);
    }

    // Decode a value based on its type definition (recursive for composite types)
    _decodeTypedValue(view, data, offset, typeDef) {
        const typeId = typeDef.typeId;

        if (typeId >= 0x01 && typeId <= 0x05) {
            // Basic type
            return this._decodeValue(view, offset, typeId);
        }

        if (typeId === MicroProtoClient.TYPES.ARRAY) {
            // ARRAY: fixed count elements (no length prefix)
            const count = typeDef.elementCount;
            const values = [];
            let bytesRead = 0;

            for (let i = 0; i < count; i++) {
                const [val, size] = this._decodeTypedValue(view, data, offset + bytesRead, typeDef.elementTypeDef);
                values.push(val);
                bytesRead += size;
            }
            return [values, bytesRead];
        }

        if (typeId === MicroProtoClient.TYPES.LIST) {
            // LIST: varint(count) + elements
            const [count, countBytes] = this._decodeVarint(data, offset);
            const values = [];
            let bytesRead = countBytes;

            for (let i = 0; i < count; i++) {
                const [val, size] = this._decodeTypedValue(view, data, offset + bytesRead, typeDef.elementTypeDef);
                values.push(val);
                bytesRead += size;
            }
            return [values, bytesRead];
        }

        if (typeId === MicroProtoClient.TYPES.OBJECT) {
            // OBJECT: field values in schema order (no overhead)
            const obj = {};
            let bytesRead = 0;

            for (const field of typeDef.fields) {
                const [val, size] = this._decodeTypedValue(view, data, offset + bytesRead, field.typeDef);
                obj[field.name] = val;
                bytesRead += size;
            }
            return [obj, bytesRead];
        }

        if (typeId === MicroProtoClient.TYPES.VARIANT) {
            // VARIANT: u8 type_index + value
            const typeIndex = data[offset];
            let bytesRead = 1;

            if (typeIndex < typeDef.variants.length) {
                const selectedVariant = typeDef.variants[typeIndex];
                const [val, size] = this._decodeTypedValue(view, data, offset + bytesRead, selectedVariant.typeDef);
                return [{ _type: selectedVariant.name, _index: typeIndex, value: val }, bytesRead + size];
            }
            return [{ _type: null, _index: typeIndex, value: null }, bytesRead];
        }

        if (typeId === MicroProtoClient.TYPES.RESOURCE) {
            // RESOURCE: varint(count) + for each: varint(id) + varint(version) + varint(size) + header_value
            const [count, countBytes] = this._decodeVarint(data, offset);
            const resources = [];
            let bytesRead = countBytes;

            for (let i = 0; i < count; i++) {
                const [id, idBytes] = this._decodeVarint(data, offset + bytesRead);
                bytesRead += idBytes;
                const [version, verBytes] = this._decodeVarint(data, offset + bytesRead);
                bytesRead += verBytes;
                const [bodySize, sizeBytes] = this._decodeVarint(data, offset + bytesRead);
                bytesRead += sizeBytes;

                const [header, headerBytes] = this._decodeTypedValue(view, data, offset + bytesRead, typeDef.headerTypeDef);
                bytesRead += headerBytes;

                resources.push({ id, version, bodySize, header });
            }
            return [resources, bytesRead];
        }

        // Unknown type
        return [null, 0];
    }

    _handleSchemaDelete(data, flags) {
        // SCHEMA_DELETE format:
        // u8 operation_header
        // [u8 batch_count] if batch flag
        // For each: u8 item_type_flags + propid item_id
        const isBatch = !!(flags & 0x01);

        let offset = 1;
        let count = 1;

        if (isBatch) {
            count = data[offset] + 1;  // Stored as count-1
            offset++;
        }

        this._log('Schema delete:', count, 'items');

        for (let i = 0; i < count && offset < data.length; i++) {
            const itemTypeFlags = data[offset++];
            const itemType = itemTypeFlags & 0x0F;  // 0=namespace, 1=property, 2=function

            const [itemId, idBytes] = this._decodePropId(data, offset);
            offset += idBytes;

            if (itemType === 1) {  // PROPERTY
                const prop = this.properties.get(itemId);
                if (prop) {
                    this.propertyByName.delete(prop.name);
                    this.properties.delete(itemId);
                    this._log('  Deleted property:', prop.name, 'id=', itemId);
                    this._emit('schemaDelete', { type: 'property', id: itemId, name: prop.name });
                }
            } else if (itemType === 2) {  // FUNCTION
                const func = this.functions.get(itemId);
                if (func) {
                    this.functionByName.delete(func.name);
                    this.functions.delete(itemId);
                    this._log('  Deleted function:', func.name, 'id=', itemId);
                    this._emit('schemaDelete', { type: 'function', id: itemId, name: func.name });
                }
            }
        }
    }

    _handleRpc(data, flags) {
        // RPC format:
        // flags: bit0=is_response, bit1=needs_response/success, bit2=has_return_value
        const isResponse = !!(flags & 0x01);

        if (isResponse) {
            this._handleRpcResponse(data, flags);
        } else {
            this._handleRpcRequest(data, flags);
        }
    }

    _handleRpcRequest(data, flags) {
        // Server calling client function (unusual but supported)
        // flags: bit1=needs_response
        const needsResponse = !!(flags & 0x02);

        let offset = 1;
        const [functionId, fidBytes] = this._decodePropId(data, offset);
        offset += fidBytes;

        let callId = null;
        if (needsResponse) {
            callId = data[offset++];
        }

        // Get function definition
        const func = this.functions.get(functionId);
        const funcName = func ? func.name : `func_${functionId}`;

        // Emit event for user code to handle
        this._log('RPC request:', funcName, 'callId=', callId);
        this._emit('rpcRequest', { functionId, functionName: funcName, callId, needsResponse, data: data.slice(offset) });
    }

    _handleRpcResponse(data, flags) {
        // Response to our RPC call
        // flags: bit1=success, bit2=has_return_value
        const success = !!(flags & 0x02);
        const hasReturnValue = !!(flags & 0x04);

        let offset = 1;
        const callId = data[offset++];

        const pending = this._pendingRpcCalls.get(callId);
        if (!pending) {
            this._log('RPC response for unknown callId:', callId);
            return;
        }

        clearTimeout(pending.timeout);
        this._pendingRpcCalls.delete(callId);

        if (success) {
            if (hasReturnValue) {
                // TODO: Decode return value based on function schema
                const returnData = data.slice(offset);
                pending.resolve({ success: true, data: returnData });
            } else {
                pending.resolve({ success: true });
            }
        } else {
            // Error response
            const errorCode = data[offset++];
            const [msgLen, msgBytes] = this._decodeVarint(data, offset);
            offset += msgBytes;
            const errorMessage = new TextDecoder().decode(data.slice(offset, offset + msgLen));

            pending.reject({ success: false, code: errorCode, message: errorMessage });
        }
    }

    _handleError(data, flags) {
        // ERROR format:
        // flags: bit0=schema_mismatch
        // u16 error_code
        // utf8 message
        const schemaMismatch = !!(flags & 0x01);
        const view = new DataView(data.buffer, data.byteOffset);

        const errorCode = view.getUint16(1, true);
        const [msgLen, varintBytes] = this._decodeVarint(data, 3);
        const message = new TextDecoder().decode(data.slice(3 + varintBytes, 3 + varintBytes + msgLen));

        this._log('Error:', errorCode, message, schemaMismatch ? '(schema mismatch)' : '');
        this._emit('error', { code: errorCode, message, schemaMismatch });

        // If schema mismatch, trigger resync
        if (schemaMismatch) {
            this._log('Schema mismatch detected, requesting resync');
            this.resync();
        }
    }

    _handleResourceGetResponse(data, flags) {
        // RESOURCE_GET response
        // flags: bit0=is_response (1), bit1=status (0=ok, 1=error)
        const isError = !!(flags & 0x02);

        let offset = 1;
        const requestId = data[offset++];

        const pending = this._pendingResourceOps.get(requestId);
        if (!pending) {
            this._log('Resource response for unknown requestId:', requestId);
            return;
        }

        clearTimeout(pending.timeout);
        this._pendingResourceOps.delete(requestId);

        if (isError) {
            const errorCode = data[offset++];
            const [msgLen, msgBytes] = this._decodeVarint(data, offset);
            offset += msgBytes;
            const errorMessage = new TextDecoder().decode(data.slice(offset, offset + msgLen));
            pending.reject({ success: false, code: errorCode, message: errorMessage });
        } else {
            // Success - body data follows
            const [bodyLen, lenBytes] = this._decodeVarint(data, offset);
            offset += lenBytes;
            const bodyData = data.slice(offset, offset + bodyLen);
            pending.resolve({ success: true, data: bodyData });
        }
    }

    _handleResourcePutResponse(data, flags) {
        // RESOURCE_PUT response
        const isError = !!(flags & 0x02);

        let offset = 1;
        const requestId = data[offset++];

        const pending = this._pendingResourceOps.get(requestId);
        if (!pending) return;

        clearTimeout(pending.timeout);
        this._pendingResourceOps.delete(requestId);

        if (isError) {
            const errorCode = data[offset++];
            const [msgLen, msgBytes] = this._decodeVarint(data, offset);
            offset += msgBytes;
            const errorMessage = new TextDecoder().decode(data.slice(offset, offset + msgLen));
            pending.reject({ success: false, code: errorCode, message: errorMessage });
        } else {
            const [resourceId, ridBytes] = this._decodeVarint(data, offset);
            pending.resolve({ success: true, resourceId });
        }
    }

    _handleResourceDeleteResponse(data, flags) {
        // RESOURCE_DELETE response
        const isError = !!(flags & 0x02);

        let offset = 1;
        const requestId = data[offset++];

        const pending = this._pendingResourceOps.get(requestId);
        if (!pending) return;

        clearTimeout(pending.timeout);
        this._pendingResourceOps.delete(requestId);

        if (isError) {
            const errorCode = data[offset++];
            const [msgLen, msgBytes] = this._decodeVarint(data, offset);
            offset += msgBytes;
            const errorMessage = new TextDecoder().decode(data.slice(offset, offset + msgLen));
            pending.reject({ success: false, code: errorCode, message: errorMessage });
        } else {
            pending.resolve({ success: true });
        }
    }

    _handlePong(data) {
        // Clear heartbeat timeout - connection is alive
        if (this._heartbeatTimeoutTimer) {
            clearTimeout(this._heartbeatTimeoutTimer);
            this._heartbeatTimeoutTimer = null;
        }
        this._lastPongTime = Date.now();
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

    // Encode varint - returns Uint8Array of bytes
    _encodeVarint(value) {
        const bytes = [];
        while (value > 0x7F) {
            bytes.push(0x80 | (value & 0x7F));
            value >>>= 7;
        }
        bytes.push(value & 0x7F);
        return new Uint8Array(bytes);
    }

    // Get size of varint encoding for a value
    _varintSize(value) {
        if (value < 128) return 1;
        if (value < 16384) return 2;
        if (value < 2097152) return 3;
        if (value < 268435456) return 4;
        return 5;
    }

    // Encode propid - returns Uint8Array (1 or 2 bytes)
    _encodePropId(id) {
        if (id < 128) {
            return new Uint8Array([id]);
        } else {
            return new Uint8Array([0x80 | (id & 0x7F), id >> 7]);
        }
    }

    // Decode propid - returns [value, bytesConsumed]
    _decodePropId(data, offset) {
        let propId = data[offset++];
        if (propId & 0x80) {
            propId = (propId & 0x7F) | (data[offset++] << 7);
            return [propId, 2];
        }
        return [propId, 1];
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

    /**
     * Request full state resync from server
     * Sends HELLO to trigger complete schema + property sync
     */
    resync() {
        if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
            this._log('Cannot resync: not connected');
            return false;
        }
        this._log('Requesting resync via HELLO');
        this._sendHello();
        return true;
    }

    /**
     * Call a remote function (RPC)
     * @param {string|number} nameOrId - Function name or ID
     * @param {ArrayBuffer|Uint8Array} params - Encoded parameters (or null for no params)
     * @param {boolean} needsResponse - Whether to wait for response (default: true)
     * @returns {Promise} Resolves with response data, rejects on error/timeout
     */
    callFunction(nameOrId, params = null, needsResponse = true) {
        return new Promise((resolve, reject) => {
            if (!this.isConnected()) {
                reject({ success: false, message: 'Not connected' });
                return;
            }

            // Look up function ID
            let funcId;
            if (typeof nameOrId === 'string') {
                funcId = this.functionByName.get(nameOrId);
                if (funcId === undefined) {
                    reject({ success: false, message: `Unknown function: ${nameOrId}` });
                    return;
                }
            } else {
                funcId = nameOrId;
            }

            // Build RPC request
            // flags: bit0=is_response (0), bit1=needs_response
            const flags = needsResponse ? 0x02 : 0x00;
            const funcIdBytes = this._encodePropId(funcId);
            const paramData = params ? new Uint8Array(params) : new Uint8Array(0);

            let messageSize = 1 + funcIdBytes.length + paramData.length;
            let callId = null;

            if (needsResponse) {
                callId = this._rpcCallId++ & 0xFF;  // Wrap at 255
                messageSize += 1;
            }

            const buf = new ArrayBuffer(messageSize);
            const bytes = new Uint8Array(buf);
            let offset = 0;

            bytes[offset++] = MicroProtoClient.OPCODES.RPC | (flags << 4);
            bytes.set(funcIdBytes, offset);
            offset += funcIdBytes.length;

            if (needsResponse) {
                bytes[offset++] = callId;
            }

            bytes.set(paramData, offset);

            // Track pending call
            if (needsResponse) {
                const timeout = setTimeout(() => {
                    this._pendingRpcCalls.delete(callId);
                    reject({ success: false, message: 'RPC timeout' });
                }, this._rpcTimeout);

                this._pendingRpcCalls.set(callId, { resolve, reject, timeout });
            }

            this._log('Calling function:', nameOrId, 'callId=', callId);
            this.ws.send(buf);

            // Fire-and-forget resolves immediately
            if (!needsResponse) {
                resolve({ success: true });
            }
        });
    }

    /**
     * Get a resource body
     * @param {string|number} propertyNameOrId - Resource property name or ID
     * @param {number} resourceId - Resource ID within the property
     * @returns {Promise} Resolves with body data
     */
    getResource(propertyNameOrId, resourceId) {
        return new Promise((resolve, reject) => {
            if (!this.isConnected()) {
                reject({ success: false, message: 'Not connected' });
                return;
            }

            // Look up property ID
            let propId;
            if (typeof propertyNameOrId === 'string') {
                propId = this.propertyByName.get(propertyNameOrId);
                if (propId === undefined) {
                    reject({ success: false, message: `Unknown property: ${propertyNameOrId}` });
                    return;
                }
            } else {
                propId = propertyNameOrId;
            }

            const requestId = this._resourceRequestId++ & 0xFF;
            const propIdBytes = this._encodePropId(propId);
            const resourceIdBytes = this._encodeVarint(resourceId);

            const buf = new ArrayBuffer(1 + 1 + propIdBytes.length + resourceIdBytes.length);
            const bytes = new Uint8Array(buf);
            let offset = 0;

            bytes[offset++] = MicroProtoClient.OPCODES.RESOURCE_GET;  // flags=0 (request)
            bytes[offset++] = requestId;
            bytes.set(propIdBytes, offset);
            offset += propIdBytes.length;
            bytes.set(resourceIdBytes, offset);

            const timeout = setTimeout(() => {
                this._pendingResourceOps.delete(requestId);
                reject({ success: false, message: 'Resource request timeout' });
            }, this._resourceTimeout);

            this._pendingResourceOps.set(requestId, { resolve, reject, timeout });

            this._log('Getting resource:', propertyNameOrId, 'id=', resourceId);
            this.ws.send(buf);
        });
    }

    /**
     * Create or update a resource
     * @param {string|number} propertyNameOrId - Resource property name or ID
     * @param {number} resourceId - 0 for new, >0 for update
     * @param {Object} options - { header: bytes, body: bytes }
     * @returns {Promise} Resolves with { success, resourceId }
     */
    putResource(propertyNameOrId, resourceId, options = {}) {
        return new Promise((resolve, reject) => {
            if (!this.isConnected()) {
                reject({ success: false, message: 'Not connected' });
                return;
            }

            let propId;
            if (typeof propertyNameOrId === 'string') {
                propId = this.propertyByName.get(propertyNameOrId);
                if (propId === undefined) {
                    reject({ success: false, message: `Unknown property: ${propertyNameOrId}` });
                    return;
                }
            } else {
                propId = propertyNameOrId;
            }

            const requestId = this._resourceRequestId++ & 0xFF;
            const propIdBytes = this._encodePropId(propId);
            const resourceIdBytes = this._encodeVarint(resourceId);

            // flags: bit1=update_header, bit2=update_body
            let flags = 0;
            const headerData = options.header ? new Uint8Array(options.header) : null;
            const bodyData = options.body ? new Uint8Array(options.body) : null;

            if (headerData) flags |= 0x02;
            if (bodyData) flags |= 0x04;

            let messageSize = 1 + 1 + propIdBytes.length + resourceIdBytes.length;
            if (headerData) messageSize += headerData.length;
            if (bodyData) messageSize += bodyData.length;

            const buf = new ArrayBuffer(messageSize);
            const bytes = new Uint8Array(buf);
            let offset = 0;

            bytes[offset++] = MicroProtoClient.OPCODES.RESOURCE_PUT | (flags << 4);
            bytes[offset++] = requestId;
            bytes.set(propIdBytes, offset);
            offset += propIdBytes.length;
            bytes.set(resourceIdBytes, offset);
            offset += resourceIdBytes.length;

            if (headerData) {
                bytes.set(headerData, offset);
                offset += headerData.length;
            }
            if (bodyData) {
                bytes.set(bodyData, offset);
            }

            const timeout = setTimeout(() => {
                this._pendingResourceOps.delete(requestId);
                reject({ success: false, message: 'Resource request timeout' });
            }, this._resourceTimeout);

            this._pendingResourceOps.set(requestId, { resolve, reject, timeout });

            this._log('Putting resource:', propertyNameOrId, 'id=', resourceId);
            this.ws.send(buf);
        });
    }

    /**
     * Delete a resource
     * @param {string|number} propertyNameOrId - Resource property name or ID
     * @param {number} resourceId - Resource ID to delete
     * @returns {Promise} Resolves with { success }
     */
    deleteResource(propertyNameOrId, resourceId) {
        return new Promise((resolve, reject) => {
            if (!this.isConnected()) {
                reject({ success: false, message: 'Not connected' });
                return;
            }

            let propId;
            if (typeof propertyNameOrId === 'string') {
                propId = this.propertyByName.get(propertyNameOrId);
                if (propId === undefined) {
                    reject({ success: false, message: `Unknown property: ${propertyNameOrId}` });
                    return;
                }
            } else {
                propId = propertyNameOrId;
            }

            const requestId = this._resourceRequestId++ & 0xFF;
            const propIdBytes = this._encodePropId(propId);
            const resourceIdBytes = this._encodeVarint(resourceId);

            const buf = new ArrayBuffer(1 + 1 + propIdBytes.length + resourceIdBytes.length);
            const bytes = new Uint8Array(buf);
            let offset = 0;

            bytes[offset++] = MicroProtoClient.OPCODES.RESOURCE_DELETE;  // flags=0 (request)
            bytes[offset++] = requestId;
            bytes.set(propIdBytes, offset);
            offset += propIdBytes.length;
            bytes.set(resourceIdBytes, offset);

            const timeout = setTimeout(() => {
                this._pendingResourceOps.delete(requestId);
                reject({ success: false, message: 'Resource request timeout' });
            }, this._resourceTimeout);

            this._pendingResourceOps.set(requestId, { resolve, reject, timeout });

            this._log('Deleting resource:', propertyNameOrId, 'id=', resourceId);
            this.ws.send(buf);
        });
    }

    /**
     * Get the schema definition for a property
     * @param {string|number} nameOrId - Property name or ID
     * @returns {Object|undefined} Property schema including type info
     */
    getPropertySchema(nameOrId) {
        let prop;
        if (typeof nameOrId === 'string') {
            const id = this.propertyByName.get(nameOrId);
            prop = this.properties.get(id);
        } else {
            prop = this.properties.get(nameOrId);
        }
        return prop;
    }
}

// Export for module systems
if (typeof module !== 'undefined' && module.exports) {
    module.exports = MicroProtoClient;
}
