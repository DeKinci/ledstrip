import { describe, it, expect } from 'vitest'
import { encodePropertyUpdate, decodePropertyUpdates, TYPES, OPCODES } from '@microproto/client'
import type { PropertySchema } from '@microproto/client'

function makeProp(id: number, name: string, typeId: number, opts: Partial<PropertySchema> = {}): PropertySchema {
  return {
    id, name, typeId, value: null, readonly: false, persistent: false, hidden: false,
    level: 0, groupId: 0, namespaceId: 0, bleExposed: false,
    description: null, constraints: {},
    ui: { color: null, colorHex: null, unit: null, icon: null, widget: 0 },
    ...opts,
  }
}

describe('encodePropertyUpdate', () => {
  it('encodes UINT8', () => {
    const prop = makeProp(3, 'brightness', TYPES.UINT8)
    const bytes = encodePropertyUpdate(3, 200, prop)
    expect(bytes[0]).toBe(OPCODES.PROPERTY_UPDATE)
    expect(bytes[1]).toBe(3)
    expect(bytes[2]).toBe(200)
    expect(bytes.length).toBe(3)
  })

  it('encodes BOOL', () => {
    const prop = makeProp(2, 'enabled', TYPES.BOOL)
    const bytes = encodePropertyUpdate(2, true, prop)
    expect(bytes[2]).toBe(1)

    const bytes2 = encodePropertyUpdate(2, false, prop)
    expect(bytes2[2]).toBe(0)
  })

  it('encodes FLOAT32', () => {
    const prop = makeProp(1, 'speed', TYPES.FLOAT32)
    const bytes = encodePropertyUpdate(1, 2.5, prop)
    expect(bytes.length).toBe(6) // 1 + 1 + 4
    const view = new DataView(bytes.buffer, bytes.byteOffset)
    expect(Math.abs(view.getFloat32(2, true) - 2.5)).toBeLessThan(0.001)
  })

  it('encodes INT32', () => {
    const prop = makeProp(5, 'count', TYPES.INT32)
    const bytes = encodePropertyUpdate(5, -12345, prop)
    const view = new DataView(bytes.buffer, bytes.byteOffset)
    expect(view.getInt32(2, true)).toBe(-12345)
  })

  it('encodes INT16', () => {
    const prop = makeProp(4, 'value', TYPES.INT16)
    const bytes = encodePropertyUpdate(4, -1000, prop)
    const view = new DataView(bytes.buffer, bytes.byteOffset)
    expect(view.getInt16(2, true)).toBe(-1000)
  })

  it('encodes LIST property', () => {
    const prop = makeProp(10, 'data', TYPES.LIST, {
      elementTypeDef: { typeId: TYPES.UINT8 },
    })
    const bytes = encodePropertyUpdate(10, [1, 2, 3], prop)
    expect(bytes[0]).toBe(OPCODES.PROPERTY_UPDATE)
    expect(bytes[1]).toBe(10)
    expect(bytes[2]).toBe(3) // varint count
    expect(bytes[3]).toBe(1)
    expect(bytes[4]).toBe(2)
    expect(bytes[5]).toBe(3)
  })
})

describe('decodePropertyUpdates', () => {
  const props = new Map<number, PropertySchema>()
  props.set(1, makeProp(1, 'speed', TYPES.FLOAT32))
  props.set(2, makeProp(2, 'enabled', TYPES.BOOL))
  props.set(3, makeProp(3, 'brightness', TYPES.UINT8))

  const getProperty = (id: number) => props.get(id)

  it('decodes single UINT8 update', () => {
    const msg = new Uint8Array([0x01, 3, 200])
    const { updates } = decodePropertyUpdates(msg, 0, getProperty)
    expect(updates.length).toBe(1)
    expect(updates[0].propertyId).toBe(3)
    expect(updates[0].value).toBe(200)
  })

  it('decodes batched updates', () => {
    const msg = new Uint8Array(2 + 5 + 2 + 2)
    const view = new DataView(msg.buffer)
    msg[0] = 0x11 // PROPERTY_UPDATE with batch flag
    msg[1] = 2    // count-1 = 2 (3 items)
    msg[2] = 1    // propId (speed)
    view.setFloat32(3, 1.5, true)
    msg[7] = 2    // propId (enabled)
    msg[8] = 1    // true
    msg[9] = 3    // propId (brightness)
    msg[10] = 128

    const { updates } = decodePropertyUpdates(msg, 0x01, getProperty)
    expect(updates.length).toBe(3)
    expect(updates[0].propertyId).toBe(1)
    expect(Math.abs(updates[0].value - 1.5)).toBeLessThan(0.001)
    expect(updates[1].value).toBe(true)
    expect(updates[2].value).toBe(128)
  })

  it('decodes with timestamp', () => {
    const msg = new Uint8Array([
      0x21,  // PROPERTY_UPDATE with batch + timestamp flags
      0,     // count-1 = 0 (1 item)
      42,    // timestamp varint
      3, 100, // propId=3, value=100
    ])
    const { updates, timestamp } = decodePropertyUpdates(msg, 0x03, getProperty)
    expect(updates.length).toBe(1)
    expect(timestamp).toBe(42)
  })

  it('stops on unknown property ID', () => {
    const msg = new Uint8Array([0x01, 99, 42]) // propId 99 not registered
    const { updates } = decodePropertyUpdates(msg, 0, getProperty)
    expect(updates.length).toBe(0)
  })
})
