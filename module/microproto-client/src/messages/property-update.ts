import { OPCODES, TYPES } from '../wire/constants.js'
import { encodeVarint, decodeVarint } from '../wire/varint.js'
import { encodePropId, decodePropId } from '../wire/propid.js'
import { encodeValue, decodeValue, getTypeSize } from '../wire/basic-types.js'
import { encodeTypedValue, decodeTypedValue } from '../wire/typed-value.js'
import type { PropertySchema } from '../types.js'

/** Encode a PROPERTY_UPDATE message for a single property. */
export function encodePropertyUpdate(propertyId: number, value: any, prop: PropertySchema): Uint8Array {
  const typeId = prop.typeId

  // Complex types: encode using schema
  if (typeId === TYPES.LIST || typeId === TYPES.OBJECT || typeId === TYPES.ARRAY) {
    const encoded = encodeTypedValue(value, prop)
    const propIdBytes = encodePropId(propertyId)
    const buf = new Uint8Array(1 + propIdBytes.length + encoded.length)
    buf[0] = OPCODES.PROPERTY_UPDATE
    buf.set(propIdBytes, 1)
    buf.set(encoded, 1 + propIdBytes.length)
    return buf
  }

  // Basic types
  const size = getTypeSize(typeId)
  const propIdSize = propertyId < 128 ? 1 : 2
  const buf = new ArrayBuffer(1 + propIdSize + size)
  const view = new DataView(buf)
  view.setUint8(0, OPCODES.PROPERTY_UPDATE)

  let offset = 1
  if (propertyId < 128) {
    view.setUint8(offset++, propertyId)
  } else {
    view.setUint8(offset++, 0x80 | (propertyId & 0x7f))
    view.setUint8(offset++, propertyId >> 7)
  }

  encodeValue(view, offset, value, typeId)
  return new Uint8Array(buf)
}

export interface DecodedPropertyUpdate {
  propertyId: number
  value: any
  bytesRead: number
}

/**
 * Decode property updates from a PROPERTY_UPDATE message.
 * Returns array of {propertyId, value} for each property in the batch.
 * `getProperty` is called to look up type info for decoding.
 */
export function decodePropertyUpdates(
  data: Uint8Array,
  flags: number,
  getProperty: (id: number) => PropertySchema | undefined,
): { updates: Array<{ propertyId: number; value: any }>; timestamp: number } {
  const isBatch = !!(flags & 0x01)
  const hasTimestamp = !!(flags & 0x02)

  let offset = 1
  let count = 1
  let timestamp = 0

  if (isBatch) {
    count = data[offset] + 1
    offset++
  }

  if (hasTimestamp) {
    const [ts, varintBytes] = decodeVarint(data, offset)
    timestamp = ts
    offset += varintBytes
  }

  const view = new DataView(data.buffer, data.byteOffset)
  const updates: Array<{ propertyId: number; value: any }> = []

  for (let i = 0; i < count && offset < data.length; i++) {
    const [propId, idBytes] = decodePropId(data, offset)
    offset += idBytes

    const prop = getProperty(propId)
    if (!prop) break // Unknown type means unknown wire size

    const typeDef = prop.elementTypeDef ? prop : { ...prop, typeId: prop.typeId }
    const [value, bytesRead] = decodeTypedValue(view, data, offset, typeDef)
    offset += bytesRead

    updates.push({ propertyId: propId, value })
  }

  return { updates, timestamp }
}
