import { describe, it, expect } from 'vitest'
import { encodeVarint, decodeVarint, varintSize } from '@microproto/client'

describe('encodeVarint', () => {
  it('encodes 0', () => {
    expect(encodeVarint(0)).toEqual(new Uint8Array([0x00]))
  })

  it('encodes 1', () => {
    expect(encodeVarint(1)).toEqual(new Uint8Array([0x01]))
  })

  it('encodes 127 (max single byte)', () => {
    expect(encodeVarint(127)).toEqual(new Uint8Array([0x7f]))
  })

  it('encodes 128 (first two-byte)', () => {
    expect(encodeVarint(128)).toEqual(new Uint8Array([0x80, 0x01]))
  })

  it('encodes 300', () => {
    // 300 = 0b100101100 → [0xAC, 0x02]
    expect(encodeVarint(300)).toEqual(new Uint8Array([0xac, 0x02]))
  })

  it('encodes 4096', () => {
    // 4096 = 0x1000 → [0x80, 0x20]
    expect(encodeVarint(4096)).toEqual(new Uint8Array([0x80, 0x20]))
  })

  it('encodes 16384 (three bytes)', () => {
    expect(encodeVarint(16384)).toEqual(new Uint8Array([0x80, 0x80, 0x01]))
  })

  it('encodes max u32 (0xFFFFFFFF)', () => {
    const result = encodeVarint(0xffffffff)
    expect(result.length).toBe(5)
  })
})

describe('decodeVarint', () => {
  it('decodes 0', () => {
    expect(decodeVarint(new Uint8Array([0x00]), 0)).toEqual([0, 1])
  })

  it('decodes 127', () => {
    expect(decodeVarint(new Uint8Array([0x7f]), 0)).toEqual([127, 1])
  })

  it('decodes 128', () => {
    expect(decodeVarint(new Uint8Array([0x80, 0x01]), 0)).toEqual([128, 2])
  })

  it('decodes 300', () => {
    expect(decodeVarint(new Uint8Array([0xac, 0x02]), 0)).toEqual([300, 2])
  })

  it('decodes 4096', () => {
    expect(decodeVarint(new Uint8Array([0x80, 0x20]), 0)).toEqual([4096, 2])
  })

  it('decodes 16384', () => {
    expect(decodeVarint(new Uint8Array([0x80, 0x80, 0x01]), 0)).toEqual([16384, 3])
  })

  it('decodes with offset into buffer', () => {
    const data = new Uint8Array([0xff, 0xff, 0x80, 0x01])
    expect(decodeVarint(data, 2)).toEqual([128, 2])
  })

  it('roundtrips all test values', () => {
    const values = [0, 1, 127, 128, 255, 300, 4096, 16383, 16384, 0xffffffff]
    for (const v of values) {
      const encoded = encodeVarint(v)
      const [decoded, bytes] = decodeVarint(encoded, 0)
      expect(decoded).toBe(v >>> 0) // unsigned
      expect(bytes).toBe(encoded.length)
    }
  })
})

describe('varintSize', () => {
  it('returns 1 for 0-127', () => {
    expect(varintSize(0)).toBe(1)
    expect(varintSize(127)).toBe(1)
  })

  it('returns 2 for 128-16383', () => {
    expect(varintSize(128)).toBe(2)
    expect(varintSize(4096)).toBe(2)
    expect(varintSize(16383)).toBe(2)
  })

  it('returns 3 for 16384-2097151', () => {
    expect(varintSize(16384)).toBe(3)
  })

  it('returns 5 for max u32', () => {
    expect(varintSize(0xffffffff)).toBe(5)
  })
})
