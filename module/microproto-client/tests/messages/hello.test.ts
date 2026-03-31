import { describe, it, expect } from 'vitest'
import { encodeHello, decodeHelloResponse } from '@microproto/client'

describe('encodeHello', () => {
  it('encodes with deviceId=1, maxPacketSize=4096', () => {
    const buf = encodeHello({ deviceId: 1, maxPacketSize: 4096, schemaVersion: 0 })
    const bytes = new Uint8Array(buf)

    expect(bytes.length).toBe(7)
    expect(bytes[0]).toBe(0x00) // HELLO opcode
    expect(bytes[1]).toBe(1)    // protocol version
    expect(bytes[2]).toBe(0x80) // maxPacketSize varint byte 0
    expect(bytes[3]).toBe(0x20) // maxPacketSize varint byte 1
    expect(bytes[4]).toBe(0x01) // deviceId varint
    expect(bytes[5]).toBe(0x00) // schemaVersion lo
    expect(bytes[6]).toBe(0x00) // schemaVersion hi
  })

  it('encodes larger deviceId', () => {
    const buf = encodeHello({ deviceId: 200, maxPacketSize: 4096, schemaVersion: 0 })
    const bytes = new Uint8Array(buf)

    expect(bytes.length).toBe(8)
    expect(bytes[4]).toBe(0xc8) // 200 varint byte 0
    expect(bytes[5]).toBe(0x01) // 200 varint byte 1
  })

  it('encodes schema version', () => {
    const buf = encodeHello({ deviceId: 1, maxPacketSize: 4096, schemaVersion: 7 })
    const bytes = new Uint8Array(buf)
    expect(bytes[bytes.length - 2]).toBe(7)  // lo
    expect(bytes[bytes.length - 1]).toBe(0)  // hi
  })

  it('encodes large schema version (u16 LE)', () => {
    const buf = encodeHello({ deviceId: 1, maxPacketSize: 4096, schemaVersion: 0x0102 })
    const bytes = new Uint8Array(buf)
    expect(bytes[bytes.length - 2]).toBe(0x02)  // lo
    expect(bytes[bytes.length - 1]).toBe(0x01)  // hi
  })
})

describe('decodeHelloResponse', () => {
  it('decodes standard response', () => {
    const response = new Uint8Array([
      0x10, // HELLO opcode with is_response flag
      1,    // version
      0x80, 0x01, // maxPacket = 128
      0x64,       // sessionId = 100
      0xc8, 0x01, // timestamp = 200
      0x05, 0x00, // schemaVersion = 5
    ])

    const result = decodeHelloResponse(response)
    expect(result).not.toBeNull()
    expect(result!.version).toBe(1)
    expect(result!.maxPacket).toBe(128)
    expect(result!.sessionId).toBe(100)
    expect(result!.timestamp).toBe(200)
    expect(result!.schemaVersion).toBe(5)
  })

  it('returns null for too-short data', () => {
    expect(decodeHelloResponse(new Uint8Array([0x10, 1, 2]))).toBeNull()
  })

  it('decodes large schema version', () => {
    const response = new Uint8Array([
      0x10, 1, 0x01, 0x01, 0x01, // header + version + single-byte varints
      0x02, 0x01, // schemaVersion = 258
    ])
    const result = decodeHelloResponse(response)
    expect(result!.schemaVersion).toBe(258)
  })
})
