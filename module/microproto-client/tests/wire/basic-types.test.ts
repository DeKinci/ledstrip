import { describe, it, expect } from 'vitest'
import { encodeValue, decodeValue, getTypeSize, TYPES } from '@microproto/client'

describe('getTypeSize', () => {
  it('returns 1 for BOOL', () => expect(getTypeSize(TYPES.BOOL)).toBe(1))
  it('returns 1 for INT8', () => expect(getTypeSize(TYPES.INT8)).toBe(1))
  it('returns 1 for UINT8', () => expect(getTypeSize(TYPES.UINT8)).toBe(1))
  it('returns 2 for INT16', () => expect(getTypeSize(TYPES.INT16)).toBe(2))
  it('returns 2 for UINT16', () => expect(getTypeSize(TYPES.UINT16)).toBe(2))
  it('returns 4 for INT32', () => expect(getTypeSize(TYPES.INT32)).toBe(4))
  it('returns 4 for FLOAT32', () => expect(getTypeSize(TYPES.FLOAT32)).toBe(4))
  it('returns 0 for unknown type', () => expect(getTypeSize(0xff)).toBe(0))
})

describe('encodeValue + decodeValue roundtrip', () => {
  function roundtrip(typeId: number, value: number) {
    const size = getTypeSize(typeId)
    const buf = new ArrayBuffer(size)
    const view = new DataView(buf)
    encodeValue(view, 0, value, typeId)
    const [decoded] = decodeValue(view, 0, typeId)
    return decoded
  }

  it('roundtrips BOOL true', () => {
    expect(roundtrip(TYPES.BOOL, 1)).toBe(true)
  })

  it('roundtrips BOOL false', () => {
    expect(roundtrip(TYPES.BOOL, 0)).toBe(false)
  })

  it('roundtrips INT8 positive', () => {
    expect(roundtrip(TYPES.INT8, 100)).toBe(100)
  })

  it('roundtrips INT8 negative', () => {
    expect(roundtrip(TYPES.INT8, -50)).toBe(-50)
  })

  it('roundtrips INT8 min/max', () => {
    expect(roundtrip(TYPES.INT8, -128)).toBe(-128)
    expect(roundtrip(TYPES.INT8, 127)).toBe(127)
  })

  it('roundtrips UINT8', () => {
    expect(roundtrip(TYPES.UINT8, 0)).toBe(0)
    expect(roundtrip(TYPES.UINT8, 200)).toBe(200)
    expect(roundtrip(TYPES.UINT8, 255)).toBe(255)
  })

  it('roundtrips INT16', () => {
    expect(roundtrip(TYPES.INT16, -1000)).toBe(-1000)
    expect(roundtrip(TYPES.INT16, 32767)).toBe(32767)
    expect(roundtrip(TYPES.INT16, -32768)).toBe(-32768)
  })

  it('roundtrips UINT16', () => {
    expect(roundtrip(TYPES.UINT16, 0)).toBe(0)
    expect(roundtrip(TYPES.UINT16, 65535)).toBe(65535)
  })

  it('roundtrips INT32', () => {
    expect(roundtrip(TYPES.INT32, -12345)).toBe(-12345)
    expect(roundtrip(TYPES.INT32, 0x7fffffff)).toBe(0x7fffffff)
  })

  it('roundtrips FLOAT32', () => {
    const val = roundtrip(TYPES.FLOAT32, 2.5)
    expect(Math.abs((val as number) - 2.5)).toBeLessThan(0.001)
  })

  it('roundtrips FLOAT32 negative', () => {
    const val = roundtrip(TYPES.FLOAT32, -3.14)
    expect(Math.abs((val as number) + 3.14)).toBeLessThan(0.01)
  })
})

describe('encodeValue', () => {
  it('encodes BOOL as 1/0', () => {
    const buf = new ArrayBuffer(1)
    const view = new DataView(buf)
    encodeValue(view, 0, true as any, TYPES.BOOL)
    expect(new Uint8Array(buf)[0]).toBe(1)

    encodeValue(view, 0, false as any, TYPES.BOOL)
    expect(new Uint8Array(buf)[0]).toBe(0)
  })

  it('encodes values as little-endian', () => {
    const buf = new ArrayBuffer(4)
    const view = new DataView(buf)
    encodeValue(view, 0, 0x0102, TYPES.UINT16)
    const bytes = new Uint8Array(buf)
    expect(bytes[0]).toBe(0x02) // little-endian: low byte first
    expect(bytes[1]).toBe(0x01)
  })
})

describe('decodeValue', () => {
  it('returns [value, bytesRead] tuple', () => {
    const buf = new ArrayBuffer(1)
    const view = new DataView(buf)
    view.setUint8(0, 42)
    const [value, bytes] = decodeValue(view, 0, TYPES.UINT8)
    expect(value).toBe(42)
    expect(bytes).toBe(1)
  })

  it('returns [null, 0] for unknown type', () => {
    const view = new DataView(new ArrayBuffer(4))
    expect(decodeValue(view, 0, 0xff)).toEqual([null, 0])
  })

  it('decodes BOOL as boolean', () => {
    const buf = new ArrayBuffer(1)
    const view = new DataView(buf)
    view.setUint8(0, 1)
    expect(decodeValue(view, 0, TYPES.BOOL)).toEqual([true, 1])
    view.setUint8(0, 0)
    expect(decodeValue(view, 0, TYPES.BOOL)).toEqual([false, 1])
  })
})
