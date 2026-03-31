import { describe, it, expect } from 'vitest'
import { encodePing, encodePong, OPCODES } from '@microproto/client'

describe('encodePing', () => {
  it('encodes with payload=0', () => {
    const buf = encodePing(0)
    const bytes = new Uint8Array(buf)
    expect(bytes.length).toBe(2)
    expect(bytes[0]).toBe(OPCODES.PING)
    expect(bytes[1]).toBe(0x00)
  })

  it('encodes with payload=42', () => {
    const buf = encodePing(42)
    const bytes = new Uint8Array(buf)
    expect(bytes[0]).toBe(OPCODES.PING)
    expect(bytes[1]).toBe(42)
  })

  it('encodes large payload as varint', () => {
    const buf = encodePing(300)
    const bytes = new Uint8Array(buf)
    expect(bytes[0]).toBe(OPCODES.PING)
    expect(bytes.length).toBe(3) // 1 header + 2 varint
  })
})

describe('encodePong', () => {
  it('sets is_response flag on original data', () => {
    const original = new Uint8Array([OPCODES.PING, 0x05])
    const buf = encodePong(original)
    const bytes = new Uint8Array(buf)

    expect(bytes[0]).toBe(OPCODES.PING | 0x10) // is_response flag
    expect(bytes[1]).toBe(0x05) // payload preserved
  })

  it('preserves multi-byte payload', () => {
    const original = new Uint8Array([OPCODES.PING, 0xac, 0x02])
    const buf = encodePong(original)
    const bytes = new Uint8Array(buf)

    expect(bytes[0]).toBe(OPCODES.PING | 0x10)
    expect(bytes[1]).toBe(0xac)
    expect(bytes[2]).toBe(0x02)
  })
})
