import { describe, it, expect } from 'vitest'
import { encodePropId, decodePropId } from '@microproto/client'

describe('encodePropId', () => {
  it('encodes 0 as single byte', () => {
    expect(encodePropId(0)).toEqual(new Uint8Array([0]))
  })

  it('encodes 50 as single byte', () => {
    expect(encodePropId(50)).toEqual(new Uint8Array([50]))
  })

  it('encodes 127 as single byte', () => {
    expect(encodePropId(127)).toEqual(new Uint8Array([127]))
  })

  it('encodes 128 as two bytes', () => {
    // 128: 0x80 | (128 & 0x7F) = 0x80, 128 >> 7 = 1
    expect(encodePropId(128)).toEqual(new Uint8Array([0x80, 0x01]))
  })

  it('encodes 200 as two bytes', () => {
    // 200: 0x80 | (200 & 0x7F) = 0xC8, 200 >> 7 = 1
    expect(encodePropId(200)).toEqual(new Uint8Array([0xc8, 0x01]))
  })

  it('encodes 255 as two bytes', () => {
    // 255: 0x80 | (255 & 0x7F) = 0xFF, 255 >> 7 = 1
    expect(encodePropId(255)).toEqual(new Uint8Array([0xff, 0x01]))
  })
})

describe('decodePropId', () => {
  it('decodes single byte (id < 128)', () => {
    expect(decodePropId(new Uint8Array([50, 0xff]), 0)).toEqual([50, 1])
  })

  it('decodes 0', () => {
    expect(decodePropId(new Uint8Array([0]), 0)).toEqual([0, 1])
  })

  it('decodes 127', () => {
    expect(decodePropId(new Uint8Array([127]), 0)).toEqual([127, 1])
  })

  it('decodes two bytes (id >= 128)', () => {
    expect(decodePropId(new Uint8Array([0xc8, 0x01, 0xff]), 0)).toEqual([200, 2])
  })

  it('decodes 128', () => {
    expect(decodePropId(new Uint8Array([0x80, 0x01]), 0)).toEqual([128, 2])
  })

  it('decodes with offset', () => {
    const data = new Uint8Array([0xaa, 0xbb, 50])
    expect(decodePropId(data, 2)).toEqual([50, 1])
  })

  it('roundtrips values', () => {
    for (const v of [0, 1, 50, 127, 128, 200, 255]) {
      const encoded = encodePropId(v)
      const [decoded, bytes] = decodePropId(encoded, 0)
      expect(decoded).toBe(v)
      expect(bytes).toBe(encoded.length)
    }
  })
})
