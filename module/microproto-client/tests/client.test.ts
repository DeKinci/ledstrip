import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { MicroProtoClient, OPCODES, TYPES, MemoryStorage } from '@microproto/client'
import { MockWebSocket } from './helpers/mock-websocket.js'

const enc = new TextEncoder()

function createClient(opts: Record<string, any> = {}) {
  let ws: MockWebSocket
  const storage = new MemoryStorage()
  const client = new MicroProtoClient('ws://test:81', {
    reconnect: false,
    deviceId: 1,
    storage,
    storageKey: opts.storageKey ?? 'test',
    webSocketFactory: (url: string) => { ws = new MockWebSocket(url); return ws },
    ...opts,
  })
  return { client, getWs: () => ws!, storage }
}

function buildHelloResponse(schemaVersion = 5) {
  return new Uint8Array([
    0x10, 1, 0x80, 0x01, 0x64, 0xc8, 0x01,
    schemaVersion & 0xff, (schemaVersion >> 8) & 0xff,
  ])
}

function buildSchemaUpsert(id: number, name: string, typeId = TYPES.UINT8) {
  const nameBytes = enc.encode(name)
  return new Uint8Array([
    0x03, 0x01, 0x00, id, 0,
    nameBytes.length, ...nameBytes,
    0, typeId, 0, 0, 0,
  ])
}

describe('MicroProtoClient', () => {
  beforeEach(() => { vi.useFakeTimers() })
  afterEach(() => { vi.useRealTimers() })

  describe('connection', () => {
    it('sends HELLO on connect', () => {
      const { client, getWs } = createClient()
      client.connect()
      const ws = getWs()
      ws.simulateOpen()

      expect(ws.sentMessages.length).toBe(1)
      const sent = ws.getSentBytes(0)
      expect(sent[0]).toBe(OPCODES.HELLO)
      expect(sent[1]).toBe(1) // protocol version
    })

    it('emits connect on HELLO response', () => {
      const { client, getWs } = createClient()
      let connectInfo: any = null
      client.on('connect', (info: any) => { connectInfo = info })

      client.connect()
      const ws = getWs()
      ws.simulateOpen()
      ws.simulateMessage(buildHelloResponse())

      expect(client.connected).toBe(true)
      expect(client.sessionId).toBe(100)
      expect(connectInfo).not.toBeNull()
      expect(connectInfo.version).toBe(1)
      expect(connectInfo.sessionId).toBe(100)
      expect(connectInfo.schemaVersion).toBe(5)
    })

    it('emits disconnect on close', () => {
      const { client, getWs } = createClient()
      let disconnected = false
      client.on('disconnect', () => { disconnected = true })

      client.connect()
      getWs().simulateOpen()
      getWs().simulateClose()

      expect(disconnected).toBe(true)
      expect(client.connected).toBe(false)
    })

    it('isConnected returns correct state', () => {
      const { client, getWs } = createClient()
      expect(client.isConnected()).toBe(false)

      client.connect()
      const ws = getWs()
      ws.simulateOpen()
      ws.simulateMessage(buildHelloResponse())
      expect(client.isConnected()).toBe(true)
    })
  })

  describe('schema', () => {
    it('registers properties from SCHEMA_UPSERT', () => {
      const { client, getWs } = createClient()
      client.connect()
      const ws = getWs()
      ws.simulateOpen()
      ws.simulateMessage(buildHelloResponse())
      ws.simulateMessage(buildSchemaUpsert(3, 'brightness'))

      expect(client.properties.size).toBe(1)
      expect(client.propertyByName.get('brightness')).toBe(3)
    })

    it('emits schema event for each item', () => {
      const { client, getWs } = createClient()
      const schemas: any[] = []
      client.on('schema', (s: any) => schemas.push(s))

      client.connect()
      const ws = getWs()
      ws.simulateOpen()
      ws.simulateMessage(buildHelloResponse())
      ws.simulateMessage(buildSchemaUpsert(1, 'a'))
      ws.simulateMessage(buildSchemaUpsert(2, 'b'))

      expect(schemas.length).toBe(2)
      expect(schemas[0].name).toBe('a')
      expect(schemas[1].name).toBe('b')
    })
  })

  describe('property updates', () => {
    it('decodes and emits property update', () => {
      const { client, getWs } = createClient()
      client.connect()
      const ws = getWs()
      ws.simulateOpen()
      ws.simulateMessage(buildHelloResponse())
      ws.simulateMessage(buildSchemaUpsert(3, 'brightness'))

      let received: any = null
      client.on('property', (id: number, name: string, value: any) => {
        received = { id, name, value }
      })

      ws.simulateMessage(new Uint8Array([0x01, 3, 200]))

      expect(received).not.toBeNull()
      expect(received.name).toBe('brightness')
      expect(received.value).toBe(200)
      expect(client.getProperty('brightness')).toBe(200)
    })

    it('emits ready after first property update', () => {
      const { client, getWs } = createClient()
      let readyFired = false
      client.on('ready', () => { readyFired = true })

      client.connect()
      const ws = getWs()
      ws.simulateOpen()
      ws.simulateMessage(buildHelloResponse())
      ws.simulateMessage(buildSchemaUpsert(1, 'test'))
      ws.simulateMessage(new Uint8Array([0x01, 1, 42]))

      expect(readyFired).toBe(true)
    })

    it('encodes and sends property update', () => {
      const { client, getWs } = createClient()
      client.connect()
      const ws = getWs()
      ws.simulateOpen()
      ws.simulateMessage(buildHelloResponse())
      ws.simulateMessage(buildSchemaUpsert(3, 'brightness'))

      const msgCountBefore = ws.sentMessages.length
      client.setProperty('brightness', 200)

      const sent = ws.getSentBytes(msgCountBefore)
      expect(sent[0]).toBe(OPCODES.PROPERTY_UPDATE)
      expect(sent[1]).toBe(3)
      expect(sent[2]).toBe(200)
    })

    it('rejects readonly property', () => {
      const { client, getWs } = createClient()
      client.connect()
      const ws = getWs()
      ws.simulateOpen()
      ws.simulateMessage(buildHelloResponse())

      // Register readonly property
      const nameBytes = enc.encode('ro')
      ws.simulateMessage(new Uint8Array([
        0x03, 0x11, 0x00, 10, 0, // item flags: property + readonly
        nameBytes.length, ...nameBytes, 0, TYPES.UINT8, 0, 0, 0,
      ]))

      const result = client.setProperty('ro', 100)
      expect(result).toBe(false)
    })

    it('rejects unknown property', () => {
      const { client, getWs } = createClient()
      client.connect()
      const ws = getWs()
      ws.simulateOpen()
      ws.simulateMessage(buildHelloResponse())

      expect(client.setProperty('nonexistent', 100)).toBe(false)
    })
  })

  describe('getProperty / getProperties', () => {
    it('getProperty returns value by name and id', () => {
      const { client, getWs } = createClient()
      client.connect()
      const ws = getWs()
      ws.simulateOpen()
      ws.simulateMessage(buildHelloResponse())
      ws.simulateMessage(buildSchemaUpsert(3, 'brightness'))
      ws.simulateMessage(new Uint8Array([0x01, 3, 200]))

      expect(client.getProperty('brightness')).toBe(200)
      expect(client.getProperty(3)).toBe(200)
      expect(client.getProperty('unknown')).toBeUndefined()
    })

    it('getProperties returns all properties', () => {
      const { client, getWs } = createClient()
      client.connect()
      const ws = getWs()
      ws.simulateOpen()
      ws.simulateMessage(buildHelloResponse())
      ws.simulateMessage(buildSchemaUpsert(1, 'a'))
      ws.simulateMessage(buildSchemaUpsert(2, 'b'))

      const props = client.getProperties()
      expect(Object.keys(props).length).toBe(2)
    })
  })

  describe('schema delete', () => {
    it('removes property on SCHEMA_DELETE', () => {
      const { client, getWs } = createClient()
      client.connect()
      const ws = getWs()
      ws.simulateOpen()
      ws.simulateMessage(buildHelloResponse())
      ws.simulateMessage(buildSchemaUpsert(5, 'testProp'))

      let deleteEvent: any = null
      client.on('schemaDelete', (info: any) => { deleteEvent = info })

      ws.simulateMessage(new Uint8Array([0x04, 0x01, 5]))

      expect(client.properties.has(5)).toBe(false)
      expect(client.propertyByName.has('testProp')).toBe(false)
      expect(deleteEvent.type).toBe('property')
      expect(deleteEvent.id).toBe(5)
    })

    it('handles batched delete', () => {
      const { client, getWs } = createClient()
      client.connect()
      const ws = getWs()
      ws.simulateOpen()
      ws.simulateMessage(buildHelloResponse())
      ws.simulateMessage(buildSchemaUpsert(1, 'a'))
      ws.simulateMessage(buildSchemaUpsert(2, 'b'))

      ws.simulateMessage(new Uint8Array([0x14, 0x01, 0x01, 1, 0x01, 2]))

      expect(client.properties.size).toBe(0)
    })
  })

  describe('error handling', () => {
    it('emits error on ERROR message', () => {
      const { client, getWs } = createClient()
      let errorEvent: any = null
      client.on('error', (err: any) => { errorEvent = err })

      client.connect()
      const ws = getWs()
      ws.simulateOpen()
      ws.simulateMessage(buildHelloResponse())

      const msgBytes = enc.encode('Bad value')
      ws.simulateMessage(new Uint8Array([0x07, 0x05, 0x00, msgBytes.length, ...msgBytes]))

      expect(errorEvent.code).toBe(5)
      expect(errorEvent.message).toBe('Bad value')
    })

    it('triggers resync on schema mismatch', () => {
      const { client, getWs } = createClient()
      client.connect()
      const ws = getWs()
      ws.simulateOpen()
      ws.simulateMessage(buildHelloResponse())

      const msgCountBefore = ws.sentMessages.length
      ws.simulateMessage(new Uint8Array([0x17, 0x02, 0x00, 0x02, 0x4f, 0x4b]))

      // Should have sent a new HELLO
      expect(ws.sentMessages.length).toBeGreaterThan(msgCountBefore)
    })
  })

  describe('heartbeat', () => {
    it('starts heartbeat after ready', () => {
      const { client, getWs } = createClient({ heartbeatInterval: 100 })
      client.connect()
      const ws = getWs()
      ws.simulateOpen()
      ws.simulateMessage(buildHelloResponse())
      ws.simulateMessage(buildSchemaUpsert(1, 'test'))
      ws.simulateMessage(new Uint8Array([0x01, 1, 42]))

      const before = ws.sentMessages.length
      vi.advanceTimersByTime(100)
      expect(ws.sentMessages.length).toBeGreaterThan(before)

      const pingMsg = ws.getSentBytes(ws.sentMessages.length - 1)
      expect(pingMsg[0]).toBe(OPCODES.PING)
    })

    it('PONG clears timeout', () => {
      const { client, getWs } = createClient({ heartbeatInterval: 100, heartbeatTimeout: 500 })
      client.connect()
      const ws = getWs()
      ws.simulateOpen()
      ws.simulateMessage(buildHelloResponse())
      ws.simulateMessage(buildSchemaUpsert(1, 'test'))
      ws.simulateMessage(new Uint8Array([0x01, 1, 42]))

      // Trigger ping
      vi.advanceTimersByTime(100)

      // Receive pong
      ws.simulateMessage(new Uint8Array([0x16, 0x00]))

      // Advancing past timeout should NOT disconnect
      let lost = false
      client.on('connectionLost', () => { lost = true })
      vi.advanceTimersByTime(600)
      expect(lost).toBe(false)
    })

    it('emits connectionLost on timeout', () => {
      const { client, getWs } = createClient({ heartbeatInterval: 5000, heartbeatTimeout: 200 })
      client.connect()
      const ws = getWs()
      ws.simulateOpen()
      ws.simulateMessage(buildHelloResponse())
      ws.simulateMessage(buildSchemaUpsert(1, 'test'))
      ws.simulateMessage(new Uint8Array([0x01, 1, 42]))

      let lost = false
      client.on('connectionLost', () => { lost = true })

      vi.advanceTimersByTime(5000) // triggers ping
      vi.advanceTimersByTime(200)  // timeout fires (no pong received)

      expect(lost).toBe(true)
    })

    it('stops on disconnect', () => {
      const { client, getWs } = createClient({ heartbeatInterval: 100 })
      client.connect()
      const ws = getWs()
      ws.simulateOpen()
      ws.simulateMessage(buildHelloResponse())
      ws.simulateMessage(buildSchemaUpsert(1, 'test'))
      ws.simulateMessage(new Uint8Array([0x01, 1, 42]))

      client.disconnect()

      const before = ws.sentMessages.length
      vi.advanceTimersByTime(500)
      // No more pings should be sent
      expect(ws.sentMessages.length).toBe(before)
    })
  })

  describe('getLastPongAge', () => {
    it('returns -1 before any pong', () => {
      const { client } = createClient()
      expect(client.getLastPongAge()).toBe(-1)
    })
  })

  describe('RPC', () => {
    it('sends RPC request with needs_response', () => {
      const { client, getWs } = createClient()
      client.connect()
      const ws = getWs()
      ws.simulateOpen()
      ws.simulateMessage(buildHelloResponse())

      // Register function manually via schema
      const fnName = enc.encode('testFunc')
      ws.simulateMessage(new Uint8Array([
        0x03, 0x02, 0x04, 1, 0, fnName.length, ...fnName, 0, 0, 0x00,
      ]))

      client.callFunction(1, null, true).catch(() => {})

      const sent = ws.getSentBytes(ws.sentMessages.length - 1)
      expect(sent[0]).toBe(0x25) // RPC + needs_response flag
      expect(sent[1]).toBe(1) // func id
      expect(sent[2]).toBe(0) // call id
    })

    it('resolves RPC on success response', async () => {
      const { client, getWs } = createClient()
      client.connect()
      const ws = getWs()
      ws.simulateOpen()
      ws.simulateMessage(buildHelloResponse())

      const fnName = enc.encode('fn')
      ws.simulateMessage(new Uint8Array([0x03, 0x02, 0x04, 1, 0, fnName.length, ...fnName, 0, 0, 0x00]))

      const promise = client.callFunction(1, null, true)
      // Success without return value: flags=3
      ws.simulateMessage(new Uint8Array([0x35, 0]))

      const result = await promise
      expect(result.success).toBe(true)
    })

    it('rejects RPC on error response', async () => {
      const { client, getWs } = createClient()
      client.connect()
      const ws = getWs()
      ws.simulateOpen()
      ws.simulateMessage(buildHelloResponse())

      const fnName = enc.encode('fn')
      ws.simulateMessage(new Uint8Array([0x03, 0x02, 0x04, 1, 0, fnName.length, ...fnName, 0, 0, 0x00]))

      const promise = client.callFunction(1, null, true)
      const msg = enc.encode('bad')
      ws.simulateMessage(new Uint8Array([0x15, 0, 3, msg.length, ...msg]))

      await expect(promise).rejects.toEqual({ success: false, code: 3, message: 'bad' })
    })

    it('wraps call_id at 255', () => {
      const { client, getWs } = createClient()
      client.connect()
      const ws = getWs()
      ws.simulateOpen()
      ws.simulateMessage(buildHelloResponse())

      const fnName = enc.encode('fn')
      ws.simulateMessage(new Uint8Array([0x03, 0x02, 0x04, 1, 0, fnName.length, ...fnName, 0, 0, 0x00]))

      // Hack internal call ID to test wrap
      ;(client as any)._rpcCallId = 255
      client.callFunction(1, null, true).catch(() => {})
      const sent1 = ws.getSentBytes(ws.sentMessages.length - 1)
      expect(sent1[2]).toBe(255)

      client.callFunction(1, null, true).catch(() => {})
      const sent2 = ws.getSentBytes(ws.sentMessages.length - 1)
      expect(sent2[2]).toBe(0) // wrapped
    })
  })

  describe('resync', () => {
    it('sends HELLO', () => {
      const { client, getWs } = createClient()
      client.connect()
      const ws = getWs()
      ws.simulateOpen()
      ws.simulateMessage(buildHelloResponse())

      const before = ws.sentMessages.length
      expect(client.resync()).toBe(true)
      expect(ws.sentMessages.length).toBe(before + 1)
    })

    it('fails when not connected', () => {
      const { client } = createClient()
      expect(client.resync()).toBe(false)
    })
  })

  describe('schema caching', () => {
    it('saves and restores schema on reconnect', () => {
      const storage = new MemoryStorage()

      // First connection
      const { client: c1, getWs: getWs1 } = createClient({ storage, storageKey: 'cache_test' })
      c1.connect()
      const ws1 = getWs1()
      ws1.simulateOpen()
      ws1.simulateMessage(buildHelloResponse(10))
      ws1.simulateMessage(buildSchemaUpsert(0, 'brightness'))
      ws1.simulateMessage(buildSchemaUpsert(1, 'speed'))
      expect(c1.properties.size).toBe(2)

      // Second connection with same storage + matching version
      const { client: c2, getWs: getWs2 } = createClient({ storage, storageKey: 'cache_test' })
      c2.connect()
      const ws2 = getWs2()
      ws2.simulateOpen()
      ws2.simulateMessage(buildHelloResponse(10)) // same version

      expect(c2.properties.size).toBe(2)
      expect(c2.propertyByName.get('brightness')).toBe(0)
    })

    it('clears schema when version differs', () => {
      const storage = new MemoryStorage()

      const { client: c1, getWs: getWs1 } = createClient({ storage, storageKey: 'diff_test' })
      c1.connect()
      const ws1 = getWs1()
      ws1.simulateOpen()
      ws1.simulateMessage(buildHelloResponse(3))
      ws1.simulateMessage(buildSchemaUpsert(0, 'test'))

      const { client: c2, getWs: getWs2 } = createClient({ storage, storageKey: 'diff_test' })
      c2.connect()
      const ws2 = getWs2()
      ws2.simulateOpen()
      ws2.simulateMessage(buildHelloResponse(4)) // different version

      expect(c2.properties.size).toBe(0)
    })
  })

  describe('event system', () => {
    it('supports on/off/emit', () => {
      const { client } = createClient()
      let count = 0
      const listener = () => count++

      client.on('test', listener)
      ;(client as any)._emit('test')
      expect(count).toBe(1)

      client.off('test', listener)
      ;(client as any)._emit('test')
      expect(count).toBe(1)
    })

    it('supports multiple listeners', () => {
      const { client } = createClient()
      let count = 0
      client.on('test', () => count++)
      client.on('test', () => count++)
      ;(client as any)._emit('test')
      expect(count).toBe(2)
    })

    it('on() returns this for chaining', () => {
      const { client } = createClient()
      const result = client.on('test', () => {})
      expect(result).toBe(client)
    })
  })
})
