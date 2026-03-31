import { OPCODES, PROTOCOL_VERSION, ERROR_CODES } from './wire/constants.js'
import { encodeHello, decodeHelloResponse } from './messages/hello.js'
import { decodeSchemaUpsert, type SchemaItem } from './messages/schema.js'
import { encodePropertyUpdate, decodePropertyUpdates } from './messages/property-update.js'
import { encodePing, encodePong } from './messages/ping.js'
import { decodeError } from './messages/error.js'
import { encodeRpcRequest, decodeRpcResponse, decodeRpcRequest } from './messages/rpc.js'
import {
  encodeResourceGet, encodeResourcePut, encodeResourceDelete,
  decodeResourceGetResponse, decodeResourcePutResponse, decodeResourceDeleteResponse,
} from './messages/resource.js'
import { encodePropId } from './wire/propid.js'
import { SchemaCache, MemoryStorage, hashUrl } from './schema-cache.js'
import type {
  ClientOptions, WebSocketLike, StorageLike, PropertySchema, FunctionSchema, WS_OPEN,
} from './types.js'

type Listener = (...args: any[]) => void
type PendingRpc = { resolve: Function; reject: Function; timeout: ReturnType<typeof setTimeout>; functionId: number }
type PendingResource = { resolve: Function; reject: Function; timeout: ReturnType<typeof setTimeout> }

export class MicroProtoClient {
  readonly url: string
  private options: Required<Pick<ClientOptions, 'deviceId' | 'maxPacketSize' | 'reconnect' | 'reconnectDelay' | 'heartbeatInterval' | 'heartbeatTimeout' | 'debug'>> & {
    rpcTimeout: number
    resourceTimeout: number
    webSocketFactory: (url: string) => WebSocketLike
  }

  private ws: WebSocketLike | null = null
  connected = false
  sessionId: number | null = null

  // Schema state
  readonly properties = new Map<number, PropertySchema>()
  readonly propertyByName = new Map<string, number>()
  readonly functions = new Map<number, FunctionSchema>()
  readonly functionByName = new Map<string, number>()

  // Internal state
  private _listeners: Record<string, Listener[]> = {}
  private _reconnectTimer: ReturnType<typeof setTimeout> | null = null
  private _heartbeatTimer: ReturnType<typeof setInterval> | null = null
  private _heartbeatTimeoutTimer: ReturnType<typeof setTimeout> | null = null
  private _lastPongTime = 0
  private _pingPayload = 0
  private _readyEmitted = false

  // RPC tracking
  private _rpcCallId = 0
  private _pendingRpcCalls = new Map<number, PendingRpc>()

  // Resource tracking
  private _resourceRequestId = 0
  private _pendingResourceOps = new Map<number, PendingResource>()

  // Schema caching
  private _schemaVersion = 0
  private _cache: SchemaCache

  constructor(url: string, options: ClientOptions = {}) {
    this.url = url
    this.options = {
      deviceId: options.deviceId ?? Math.floor(Math.random() * 0xffffffff),
      maxPacketSize: options.maxPacketSize ?? 4096,
      reconnect: options.reconnect !== false,
      reconnectDelay: options.reconnectDelay ?? 2000,
      heartbeatInterval: options.heartbeatInterval ?? 5000,
      heartbeatTimeout: options.heartbeatTimeout ?? 10000,
      debug: options.debug ?? false,
      rpcTimeout: options.rpcTimeout ?? 60000,
      resourceTimeout: options.resourceTimeout ?? 30000,
      webSocketFactory: options.webSocketFactory ?? ((u: string) => new WebSocket(u)),
    }

    const storage: StorageLike = options.storage ?? new MemoryStorage()
    const storageKey = options.storageKey ?? ('mp_' + hashUrl(url))
    this._cache = new SchemaCache(storage, storageKey)
  }

  // ===== Event System =====

  on(event: string, callback: Listener): this {
    if (!this._listeners[event]) this._listeners[event] = []
    this._listeners[event].push(callback)
    return this
  }

  off(event: string, callback: Listener): this {
    if (this._listeners[event]) {
      this._listeners[event] = this._listeners[event].filter(cb => cb !== callback)
    }
    return this
  }

  private _emit(event: string, ...args: any[]): void {
    if (this._listeners[event]) {
      this._listeners[event].forEach(cb => cb(...args))
    }
  }

  private _log(...args: any[]): void {
    if (this.options.debug) console.log('[MicroProto]', ...args)
  }

  // ===== Connection Management =====

  connect(): void {
    if (this.ws && this.ws.readyState === 1) return

    this._log('Connecting to', this.url)
    this.ws = this.options.webSocketFactory(this.url)
    this.ws.binaryType = 'arraybuffer'

    this.ws.onopen = () => {
      this._log('WebSocket connected, sending HELLO')
      this._sendHello()
    }

    this.ws.onmessage = (event: any) => {
      if (event.data instanceof ArrayBuffer) {
        this._handleMessage(new Uint8Array(event.data))
      }
    }

    this.ws.onerror = (error: any) => {
      this._log('WebSocket error:', error)
      this._emit('error', error)
    }

    this.ws.onclose = () => {
      this._log('WebSocket closed')
      this.connected = false
      this._stopHeartbeat()
      this._readyEmitted = false
      this._emit('disconnect')

      if (this.options.reconnect) {
        this._scheduleReconnect()
      }
    }
  }

  disconnect(): void {
    this.options.reconnect = false
    this._stopHeartbeat()
    if (this._reconnectTimer) {
      clearTimeout(this._reconnectTimer)
      this._reconnectTimer = null
    }
    if (this.ws) {
      this.ws.close()
      this.ws = null
    }
  }

  isConnected(): boolean {
    return this.connected && this.ws !== null && this.ws.readyState === 1
  }

  // ===== Heartbeat =====

  private _startHeartbeat(): void {
    this._stopHeartbeat()
    this._lastPongTime = Date.now()
    this._heartbeatTimer = setInterval(() => this._sendPing(), this.options.heartbeatInterval)
  }

  private _stopHeartbeat(): void {
    if (this._heartbeatTimer) { clearInterval(this._heartbeatTimer); this._heartbeatTimer = null }
    if (this._heartbeatTimeoutTimer) { clearTimeout(this._heartbeatTimeoutTimer); this._heartbeatTimeoutTimer = null }
  }

  private _sendPing(): void {
    if (!this.isConnected()) return
    const payload = this._pingPayload++
    this.ws!.send(encodePing(payload))

    if (this._heartbeatTimeoutTimer) clearTimeout(this._heartbeatTimeoutTimer)
    this._heartbeatTimeoutTimer = setTimeout(() => this._onHeartbeatTimeout(), this.options.heartbeatTimeout)
  }

  private _onHeartbeatTimeout(): void {
    this._stopHeartbeat()
    this._emit('connectionLost')
    if (this.ws) this.ws.close()
  }

  private _scheduleReconnect(): void {
    if (this._reconnectTimer) return
    this._reconnectTimer = setTimeout(() => {
      this._reconnectTimer = null
      this.connect()
    }, this.options.reconnectDelay)
  }

  // ===== Protocol: Send =====

  private _sendHello(): void {
    const cachedVersion = this._cache.loadSchemaVersion()
    this.ws!.send(encodeHello({
      deviceId: this.options.deviceId,
      maxPacketSize: this.options.maxPacketSize,
      schemaVersion: cachedVersion,
    }))
  }

  // ===== Protocol: Receive =====

  private _handleMessage(data: Uint8Array): void {
    if (data.length === 0) return

    const header = data[0]
    const opcode = header & 0x0f
    const flags = (header >> 4) & 0x0f

    switch (opcode) {
      case OPCODES.HELLO: this._handleHelloResponse(data, flags); break
      case OPCODES.SCHEMA_UPSERT: this._handleSchemaUpsert(data, flags); break
      case OPCODES.SCHEMA_DELETE: this._handleSchemaDelete(data, flags); break
      case OPCODES.PROPERTY_UPDATE: this._handlePropertyUpdate(data, flags); break
      case OPCODES.RPC: {
        if (flags & 0x01) this._handleRpcResponse(data, flags)
        else this._handleRpcRequest(data, flags)
        break
      }
      case OPCODES.ERROR: this._handleError(data, flags); break
      case OPCODES.PING: {
        if (flags & 0x01) this._handlePong()
        else this.ws?.send(encodePong(data))
        break
      }
      case OPCODES.RESOURCE_GET: this._handleResourceGetResponse(data, flags); break
      case OPCODES.RESOURCE_PUT: this._handleResourcePutResponse(data, flags); break
      case OPCODES.RESOURCE_DELETE: this._handleResourceDeleteResponse(data, flags); break
    }
  }

  private _handleHelloResponse(data: Uint8Array, flags: number): void {
    const resp = decodeHelloResponse(data)
    if (!resp) {
      this._emit('error', { code: ERROR_CODES.BUFFER_OVERFLOW, message: 'HELLO response too short' })
      return
    }

    if (resp.version !== PROTOCOL_VERSION) {
      this._emit('error', { code: ERROR_CODES.PROTOCOL_VERSION_MISMATCH, message: 'Protocol version mismatch' })
      return
    }

    this.sessionId = resp.sessionId
    this.connected = true

    const cachedVersion = this._cache.loadSchemaVersion()
    const schemaMatch = cachedVersion !== 0 && cachedVersion === resp.schemaVersion

    if (schemaMatch) {
      const cached = this._cache.loadSchema()
      if (!cached) {
        this._schemaVersion = 0
        this._cache.saveSchemaVersion(0)
        this._sendHello()
        return
      }
      this.properties.clear()
      this.propertyByName.clear()
      for (const [id, prop] of cached) {
        this.properties.set(id, prop)
        this.propertyByName.set(prop.name, id)
      }
      for (const [, prop] of this.properties) {
        this._emit('schema', prop)
      }
    } else {
      this.properties.clear()
      this.propertyByName.clear()
      this.functions.clear()
      this.functionByName.clear()
    }

    this._schemaVersion = resp.schemaVersion
    this._cache.saveSchemaVersion(resp.schemaVersion)

    this._emit('connect', {
      version: resp.version, maxPacket: resp.maxPacket,
      sessionId: resp.sessionId, timestamp: resp.timestamp,
      schemaVersion: resp.schemaVersion, schemaMatch,
    })
  }

  private _handleSchemaUpsert(data: Uint8Array, flags: number): void {
    const items = decodeSchemaUpsert(data, flags)

    for (const item of items) {
      if ('type' in item && item.type === 'function') {
        this.functions.set(item.id, item as FunctionSchema)
        this.functionByName.set(item.name, item.id)
      } else {
        this.properties.set(item.id, item as PropertySchema)
        this.propertyByName.set(item.name, item.id)
      }
      this._emit('schema', item)
    }

    this._cache.saveSchema(this.properties)
  }

  private _handleSchemaDelete(data: Uint8Array, flags: number): void {
    const isBatch = !!(flags & 0x01)
    let offset = 1
    let count = 1

    if (isBatch) {
      count = data[offset] + 1
      offset++
    }

    for (let i = 0; i < count && offset < data.length; i++) {
      const itemTypeFlags = data[offset++]
      const itemType = itemTypeFlags & 0x0f

      let itemId = data[offset++]
      if (itemId & 0x80) {
        itemId = (itemId & 0x7f) | (data[offset++] << 7)
      }

      if (itemType === 1) {
        const prop = this.properties.get(itemId)
        if (prop) {
          this.propertyByName.delete(prop.name)
          this.properties.delete(itemId)
          this._emit('schemaDelete', { type: 'property', id: itemId, name: prop.name })
        }
      } else if (itemType === 2) {
        const func = this.functions.get(itemId)
        if (func) {
          this.functionByName.delete(func.name)
          this.functions.delete(itemId)
          this._emit('schemaDelete', { type: 'function', id: itemId, name: func.name })
        }
      }
    }
  }

  private _handlePropertyUpdate(data: Uint8Array, flags: number): void {
    const { updates, timestamp } = decodePropertyUpdates(data, flags, (id) => this.properties.get(id))

    for (const { propertyId, value } of updates) {
      const prop = this.properties.get(propertyId)
      if (!prop) continue
      const oldValue = prop.value
      prop.value = value
      this._emit('property', propertyId, prop.name, value, oldValue)
    }

    if (!this._readyEmitted && this.properties.size > 0) {
      this._readyEmitted = true
      this._emit('ready', this.getProperties())
      this._startHeartbeat()
    }
  }

  private _handleRpcRequest(data: Uint8Array, flags: number): void {
    const req = decodeRpcRequest(data, flags)
    const func = this.functions.get(req.functionId)
    const funcName = func ? func.name : `func_${req.functionId}`
    this._emit('rpcRequest', {
      functionId: req.functionId, functionName: funcName,
      callId: req.callId, needsResponse: req.needsResponse, data: req.data,
    })
  }

  private _handleRpcResponse(data: Uint8Array, flags: number): void {
    const resp = decodeRpcResponse(data, flags, (callId) => {
      const pending = this._pendingRpcCalls.get(callId)
      if (!pending) return null
      const func = this.functions.get(pending.functionId)
      return func?.returnTypeDef ?? null
    })

    const pending = this._pendingRpcCalls.get(resp.callId)
    if (!pending) return

    clearTimeout(pending.timeout)
    this._pendingRpcCalls.delete(resp.callId)

    if (resp.success) {
      if (resp.value !== undefined) pending.resolve({ success: true, value: resp.value })
      else if (resp.data) pending.resolve({ success: true, data: resp.data })
      else pending.resolve({ success: true })
    } else {
      pending.reject({ success: false, code: resp.errorCode, message: resp.errorMessage })
    }
  }

  private _handleError(data: Uint8Array, flags: number): void {
    const err = decodeError(data, flags)
    this._emit('error', err)
    if (err.schemaMismatch) this.resync()
  }

  private _handlePong(): void {
    if (this._heartbeatTimeoutTimer) {
      clearTimeout(this._heartbeatTimeoutTimer)
      this._heartbeatTimeoutTimer = null
    }
    this._lastPongTime = Date.now()
  }

  private _handleResourceGetResponse(data: Uint8Array, flags: number): void {
    const resp = decodeResourceGetResponse(data, flags)
    this._resolveResourceOp(resp.requestId, resp)
  }

  private _handleResourcePutResponse(data: Uint8Array, flags: number): void {
    const resp = decodeResourcePutResponse(data, flags)
    this._resolveResourceOp(resp.requestId, resp)
  }

  private _handleResourceDeleteResponse(data: Uint8Array, flags: number): void {
    const resp = decodeResourceDeleteResponse(data, flags)
    this._resolveResourceOp(resp.requestId, resp)
  }

  private _resolveResourceOp(requestId: number, resp: any): void {
    const pending = this._pendingResourceOps.get(requestId)
    if (!pending) return
    clearTimeout(pending.timeout)
    this._pendingResourceOps.delete(requestId)

    if (resp.success) pending.resolve(resp)
    else pending.reject(resp)
  }

  // ===== Public API =====

  setProperty(nameOrId: string | number, value: any): boolean {
    let prop: PropertySchema | undefined

    if (typeof nameOrId === 'string') {
      const id = this.propertyByName.get(nameOrId)
      if (id === undefined) return false
      prop = this.properties.get(id)
    } else {
      prop = this.properties.get(nameOrId)
    }

    if (!prop) return false
    if (prop.readonly) return false
    if (!this.isConnected()) return false

    this.ws!.send(encodePropertyUpdate(prop.id, value, prop))
    return true
  }

  getProperty(nameOrId: string | number): any {
    let prop: PropertySchema | undefined
    if (typeof nameOrId === 'string') {
      const id = this.propertyByName.get(nameOrId)
      prop = id !== undefined ? this.properties.get(id) : undefined
    } else {
      prop = this.properties.get(nameOrId)
    }
    return prop ? prop.value : undefined
  }

  getProperties(): Record<string, { id: number; value: any; type: number; readonly: boolean }> {
    const result: Record<string, any> = {}
    for (const [, prop] of this.properties) {
      result[prop.name] = { id: prop.id, value: prop.value, type: prop.typeId, readonly: prop.readonly }
    }
    return result
  }

  getPropertySchema(nameOrId: string | number): PropertySchema | undefined {
    if (typeof nameOrId === 'string') {
      const id = this.propertyByName.get(nameOrId)
      return id !== undefined ? this.properties.get(id) : undefined
    }
    return this.properties.get(nameOrId)
  }

  getLastPongAge(): number {
    if (this._lastPongTime === 0) return -1
    return Date.now() - this._lastPongTime
  }

  resync(): boolean {
    if (!this.ws || this.ws.readyState !== 1) return false
    this._sendHello()
    return true
  }

  callFunction(nameOrId: string | number, params: ArrayBuffer | Uint8Array | null = null, needsResponse = true): Promise<any> {
    return new Promise((resolve, reject) => {
      if (!this.isConnected()) { reject({ success: false, message: 'Not connected' }); return }

      let funcId: number
      if (typeof nameOrId === 'string') {
        const id = this.functionByName.get(nameOrId)
        if (id === undefined) { reject({ success: false, message: `Unknown function: ${nameOrId}` }); return }
        funcId = id
      } else {
        funcId = nameOrId
      }

      let callId: number | null = null
      if (needsResponse) {
        callId = this._rpcCallId++ & 0xff
      }

      this.ws!.send(encodeRpcRequest(funcId, callId, needsResponse, params))

      if (needsResponse && callId !== null) {
        const timeout = setTimeout(() => {
          this._pendingRpcCalls.delete(callId!)
          reject({ success: false, message: 'RPC timeout' })
        }, this.options.rpcTimeout)
        this._pendingRpcCalls.set(callId, { resolve, reject, timeout, functionId: funcId })
      } else {
        resolve({ success: true })
      }
    })
  }

  getResource(propertyNameOrId: string | number, resourceId: number): Promise<any> {
    return this._resourceOp(propertyNameOrId, (requestId, propId) =>
      encodeResourceGet(requestId, propId, resourceId))
  }

  putResource(propertyNameOrId: string | number, resourceId: number, options: { header?: Uint8Array; body?: Uint8Array } = {}): Promise<any> {
    return this._resourceOp(propertyNameOrId, (requestId, propId) =>
      encodeResourcePut(requestId, propId, resourceId, options))
  }

  deleteResource(propertyNameOrId: string | number, resourceId: number): Promise<any> {
    return this._resourceOp(propertyNameOrId, (requestId, propId) =>
      encodeResourceDelete(requestId, propId, resourceId))
  }

  private _resourceOp(propertyNameOrId: string | number, buildMessage: (requestId: number, propId: number) => ArrayBuffer): Promise<any> {
    return new Promise((resolve, reject) => {
      if (!this.isConnected()) { reject({ success: false, message: 'Not connected' }); return }

      let propId: number
      if (typeof propertyNameOrId === 'string') {
        const id = this.propertyByName.get(propertyNameOrId)
        if (id === undefined) { reject({ success: false, message: `Unknown property: ${propertyNameOrId}` }); return }
        propId = id
      } else {
        propId = propertyNameOrId
      }

      const requestId = this._resourceRequestId++ & 0xff
      const timeout = setTimeout(() => {
        this._pendingResourceOps.delete(requestId)
        reject({ success: false, message: 'Resource request timeout' })
      }, this.options.resourceTimeout)

      this._pendingResourceOps.set(requestId, { resolve, reject, timeout })
      this.ws!.send(buildMessage(requestId, propId))
    })
  }
}
