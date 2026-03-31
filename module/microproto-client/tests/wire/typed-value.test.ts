import { describe, it, expect } from 'vitest'
import { encodeTypedValue, decodeTypedValue, getValueSize, TYPES } from '@microproto/client'
import type { TypeDef } from '@microproto/client'

function decode(data: Uint8Array, typeDef: TypeDef) {
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength)
  return decodeTypedValue(view, data, 0, typeDef)
}

function roundtrip(value: any, typeDef: TypeDef) {
  const encoded = encodeTypedValue(value, typeDef)
  const [decoded] = decode(encoded, typeDef)
  return decoded
}

// Type definition helpers
const u8: TypeDef = { typeId: TYPES.UINT8 }
const i32: TypeDef = { typeId: TYPES.INT32 }
const f32: TypeDef = { typeId: TYPES.FLOAT32 }
const bool: TypeDef = { typeId: TYPES.BOOL }

describe('ARRAY encode/decode', () => {
  const rgb: TypeDef = { typeId: TYPES.ARRAY, elementCount: 3, elementTypeDef: u8 }

  it('encodes fixed-count array without length prefix', () => {
    const encoded = encodeTypedValue([10, 20, 30], rgb)
    expect(encoded).toEqual(new Uint8Array([10, 20, 30]))
  })

  it('decodes fixed-count array', () => {
    const [value, bytes] = decode(new Uint8Array([10, 20, 30]), rgb)
    expect(value).toEqual([10, 20, 30])
    expect(bytes).toBe(3)
  })

  it('roundtrips', () => {
    expect(roundtrip([255, 0, 128], rgb)).toEqual([255, 0, 128])
  })

  it('pads with 0 for short arrays', () => {
    const encoded = encodeTypedValue([10], rgb)
    expect(encoded).toEqual(new Uint8Array([10, 0, 0]))
  })

  it('handles ARRAY of INT32', () => {
    const arr: TypeDef = { typeId: TYPES.ARRAY, elementCount: 2, elementTypeDef: i32 }
    expect(roundtrip([100, 200], arr)).toEqual([100, 200])
  })
})

describe('LIST encode/decode', () => {
  const listU8: TypeDef = { typeId: TYPES.LIST, elementTypeDef: u8 }

  it('encodes with varint length prefix', () => {
    const encoded = encodeTypedValue([1, 2, 3], listU8)
    // varint(3) = 0x03, then values
    expect(encoded).toEqual(new Uint8Array([3, 1, 2, 3]))
  })

  it('decodes list', () => {
    const [value, bytes] = decode(new Uint8Array([3, 10, 20, 30]), listU8)
    expect(value).toEqual([10, 20, 30])
    expect(bytes).toBe(4)
  })

  it('handles empty list', () => {
    const encoded = encodeTypedValue([], listU8)
    expect(encoded).toEqual(new Uint8Array([0]))
    const [value] = decode(new Uint8Array([0]), listU8)
    expect(value).toEqual([])
  })

  it('roundtrips', () => {
    expect(roundtrip([100, 200, 50], listU8)).toEqual([100, 200, 50])
  })

  it('handles LIST of FLOAT32', () => {
    const listF32: TypeDef = { typeId: TYPES.LIST, elementTypeDef: f32 }
    const val = roundtrip([1.5, 2.5], listF32)
    expect(Math.abs(val[0] - 1.5)).toBeLessThan(0.01)
    expect(Math.abs(val[1] - 2.5)).toBeLessThan(0.01)
  })
})

describe('OBJECT encode/decode', () => {
  const point: TypeDef = {
    typeId: TYPES.OBJECT,
    fields: [
      { name: 'x', typeDef: i32 },
      { name: 'y', typeDef: i32 },
    ],
  }

  it('encodes fields in schema order', () => {
    const encoded = encodeTypedValue({ x: 100, y: 200 }, point)
    expect(encoded.length).toBe(8) // 4 + 4
  })

  it('decodes fields into named object', () => {
    const data = new Uint8Array(8)
    const view = new DataView(data.buffer)
    view.setInt32(0, 100, true)
    view.setInt32(4, 200, true)
    const [value, bytes] = decode(data, point)
    expect(value).toEqual({ x: 100, y: 200 })
    expect(bytes).toBe(8)
  })

  it('roundtrips', () => {
    expect(roundtrip({ x: -50, y: 300 }, point)).toEqual({ x: -50, y: 300 })
  })

  it('handles mixed types', () => {
    const mixed: TypeDef = {
      typeId: TYPES.OBJECT,
      fields: [
        { name: 'enabled', typeDef: bool },
        { name: 'count', typeDef: u8 },
      ],
    }
    expect(roundtrip({ enabled: true, count: 42 }, mixed)).toEqual({ enabled: true, count: 42 })
  })
})

describe('VARIANT encode/decode', () => {
  const myVariant: TypeDef = {
    typeId: TYPES.VARIANT,
    variants: [
      { name: 'off', typeDef: bool },
      { name: 'level', typeDef: u8 },
    ],
  }

  it('encodes with type index prefix', () => {
    const encoded = encodeTypedValue({ _index: 1, value: 42 }, myVariant)
    expect(encoded).toEqual(new Uint8Array([1, 42]))
  })

  it('decodes variant', () => {
    const [value, bytes] = decode(new Uint8Array([1, 42]), myVariant)
    expect(value).toEqual({ _type: 'level', _index: 1, value: 42 })
    expect(bytes).toBe(2)
  })

  it('decodes first variant', () => {
    const [value] = decode(new Uint8Array([0, 1]), myVariant)
    expect(value).toEqual({ _type: 'off', _index: 0, value: true })
  })

  it('handles out-of-range index', () => {
    const [value] = decode(new Uint8Array([5]), myVariant)
    expect(value._type).toBeNull()
    expect(value._index).toBe(5)
  })
})

describe('STREAM encode/decode', () => {
  const stream: TypeDef = { typeId: TYPES.STREAM, elementTypeDef: u8, historyCapacity: 10 }

  it('uses same wire format as LIST', () => {
    const encoded = encodeTypedValue([1, 2, 3], stream)
    expect(encoded).toEqual(new Uint8Array([3, 1, 2, 3]))
  })

  it('roundtrips', () => {
    expect(roundtrip([10, 20], stream)).toEqual([10, 20])
  })
})

describe('RESOURCE decode', () => {
  const resource: TypeDef = {
    typeId: TYPES.RESOURCE,
    headerTypeDef: { typeId: TYPES.OBJECT, fields: [{ name: 'name', typeDef: u8 }] },
    bodyTypeDef: u8,
  }

  it('decodes resource list', () => {
    // count=1, id=5, version=2, bodySize=100, header(name=42)
    const data = new Uint8Array([1, 5, 2, 100, 42])
    const [value, bytes] = decode(data, resource)
    expect(value).toEqual([{ id: 5, version: 2, bodySize: 100, header: { name: 42 } }])
    expect(bytes).toBe(5)
  })

  it('decodes empty resource list', () => {
    const [value] = decode(new Uint8Array([0]), resource)
    expect(value).toEqual([])
  })
})

describe('nested composite types', () => {
  it('handles LIST of OBJECT', () => {
    const listOfObj: TypeDef = {
      typeId: TYPES.LIST,
      elementTypeDef: {
        typeId: TYPES.OBJECT,
        fields: [
          { name: 'a', typeDef: u8 },
          { name: 'b', typeDef: u8 },
        ],
      },
    }
    const result = roundtrip([{ a: 1, b: 2 }, { a: 3, b: 4 }], listOfObj)
    expect(result).toEqual([{ a: 1, b: 2 }, { a: 3, b: 4 }])
  })

  it('handles ARRAY of LIST', () => {
    const arrayOfList: TypeDef = {
      typeId: TYPES.ARRAY,
      elementCount: 2,
      elementTypeDef: { typeId: TYPES.LIST, elementTypeDef: u8 },
    }
    const result = roundtrip([[1, 2], [3, 4, 5]], arrayOfList)
    expect(result).toEqual([[1, 2], [3, 4, 5]])
  })
})

describe('getValueSize', () => {
  it('returns basic type sizes', () => {
    expect(getValueSize(u8, new Uint8Array([42]), 0)).toBe(1)
    expect(getValueSize(i32, new Uint8Array(4), 0)).toBe(4)
  })

  it('returns ARRAY size', () => {
    const rgb: TypeDef = { typeId: TYPES.ARRAY, elementCount: 3, elementTypeDef: u8 }
    expect(getValueSize(rgb, new Uint8Array([10, 20, 30]), 0)).toBe(3)
  })

  it('returns LIST size including count prefix', () => {
    const list: TypeDef = { typeId: TYPES.LIST, elementTypeDef: u8 }
    // varint(2) = 1 byte + 2 values = 3
    expect(getValueSize(list, new Uint8Array([2, 10, 20]), 0)).toBe(3)
  })

  it('returns OBJECT size', () => {
    const point: TypeDef = {
      typeId: TYPES.OBJECT,
      fields: [
        { name: 'x', typeDef: i32 },
        { name: 'y', typeDef: i32 },
      ],
    }
    expect(getValueSize(point, new Uint8Array(8), 0)).toBe(8)
  })
})
