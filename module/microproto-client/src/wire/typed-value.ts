import { TYPES } from './constants.js'
import { encodeVarint, decodeVarint } from './varint.js'
import { encodeValue, decodeValue, getTypeSize } from './basic-types.js'
import type { TypeDef } from '../types.js'

/** Encode a typed value recursively. Returns a Uint8Array. */
export function encodeTypedValue(value: any, typeDef: TypeDef): Uint8Array {
  const parts: Uint8Array[] = []
  encodeTypedValueInto(parts, value, typeDef)
  const totalLen = parts.reduce((sum, p) => sum + p.length, 0)
  const result = new Uint8Array(totalLen)
  let off = 0
  for (const p of parts) {
    result.set(p, off)
    off += p.length
  }
  return result
}

function encodeTypedValueInto(parts: Uint8Array[], value: any, typeDef: TypeDef): void {
  const typeId = typeDef.typeId

  if (typeId === TYPES.LIST || typeId === TYPES.STREAM) {
    const arr = Array.isArray(value) ? value : []
    parts.push(encodeVarint(arr.length))
    for (const elem of arr) {
      encodeTypedValueInto(parts, elem, typeDef.elementTypeDef!)
    }
    return
  }

  if (typeId === TYPES.ARRAY) {
    const arr = Array.isArray(value) ? value : []
    const count = typeDef.elementCount || arr.length
    for (let i = 0; i < count; i++) {
      encodeTypedValueInto(parts, arr[i] ?? 0, typeDef.elementTypeDef!)
    }
    return
  }

  if (typeId === TYPES.OBJECT) {
    const fields = typeDef.fields || []
    for (const field of fields) {
      const fieldVal = value ? value[field.name] : 0
      encodeTypedValueInto(parts, fieldVal, field.typeDef)
    }
    return
  }

  if (typeId === TYPES.VARIANT) {
    const typeIndex = value?._index ?? 0
    const buf = new Uint8Array([typeIndex])
    parts.push(buf)
    const selected = typeDef.variants?.[typeIndex]
    if (selected) {
      encodeTypedValueInto(parts, value?.value ?? 0, selected.typeDef)
    }
    return
  }

  // Basic type
  const size = getTypeSize(typeId)
  const buf = new ArrayBuffer(size)
  const view = new DataView(buf)
  encodeValue(view, 0, value ?? 0, typeId)
  parts.push(new Uint8Array(buf))
}

/** Decode a typed value recursively. Returns [value, bytesRead]. */
export function decodeTypedValue(
  view: DataView,
  data: Uint8Array,
  offset: number,
  typeDef: TypeDef,
): [any, number] {
  const typeId = typeDef.typeId

  // Basic types
  if (typeId >= 0x01 && typeId <= 0x07) {
    return decodeValue(view, offset, typeId)
  }

  if (typeId === TYPES.ARRAY) {
    const count = typeDef.elementCount!
    const values: any[] = []
    let bytesRead = 0
    for (let i = 0; i < count; i++) {
      const [val, size] = decodeTypedValue(view, data, offset + bytesRead, typeDef.elementTypeDef!)
      values.push(val)
      bytesRead += size
    }
    return [values, bytesRead]
  }

  if (typeId === TYPES.LIST || typeId === TYPES.STREAM) {
    const [count, countBytes] = decodeVarint(data, offset)
    const values: any[] = []
    let bytesRead = countBytes
    for (let i = 0; i < count; i++) {
      const [val, size] = decodeTypedValue(view, data, offset + bytesRead, typeDef.elementTypeDef!)
      values.push(val)
      bytesRead += size
    }
    return [values, bytesRead]
  }

  if (typeId === TYPES.OBJECT) {
    const obj: Record<string, any> = {}
    let bytesRead = 0
    for (const field of typeDef.fields!) {
      const [val, size] = decodeTypedValue(view, data, offset + bytesRead, field.typeDef)
      obj[field.name] = val
      bytesRead += size
    }
    return [obj, bytesRead]
  }

  if (typeId === TYPES.VARIANT) {
    const typeIndex = data[offset]
    let bytesRead = 1
    if (typeIndex < typeDef.variants!.length) {
      const selected = typeDef.variants![typeIndex]
      const [val, size] = decodeTypedValue(view, data, offset + bytesRead, selected.typeDef)
      return [{ _type: selected.name, _index: typeIndex, value: val }, bytesRead + size]
    }
    return [{ _type: null, _index: typeIndex, value: null }, bytesRead]
  }

  if (typeId === TYPES.RESOURCE) {
    const [count, countBytes] = decodeVarint(data, offset)
    const resources: any[] = []
    let bytesRead = countBytes
    for (let i = 0; i < count; i++) {
      const [id, idBytes] = decodeVarint(data, offset + bytesRead)
      bytesRead += idBytes
      const [version, verBytes] = decodeVarint(data, offset + bytesRead)
      bytesRead += verBytes
      const [bodySize, sizeBytes] = decodeVarint(data, offset + bytesRead)
      bytesRead += sizeBytes
      const [header, headerBytes] = decodeTypedValue(view, data, offset + bytesRead, typeDef.headerTypeDef!)
      bytesRead += headerBytes
      resources.push({ id, version, bodySize, header })
    }
    return [resources, bytesRead]
  }

  return [null, 0]
}

/** Calculate the wire size of a value based on its type definition. */
export function getValueSize(typeDef: TypeDef, data: Uint8Array, offset: number): number {
  const typeId = typeDef.typeId

  if (typeId === TYPES.ARRAY) {
    let size = 0
    for (let i = 0; i < typeDef.elementCount!; i++) {
      size += getValueSize(typeDef.elementTypeDef!, data, offset + size)
    }
    return size
  }

  if (typeId === TYPES.LIST || typeId === TYPES.STREAM) {
    const [count, countBytes] = decodeVarint(data, offset)
    let size = countBytes
    for (let i = 0; i < count; i++) {
      size += getValueSize(typeDef.elementTypeDef!, data, offset + size)
    }
    return size
  }

  if (typeId === TYPES.OBJECT) {
    let size = 0
    for (const field of typeDef.fields!) {
      size += getValueSize(field.typeDef, data, offset + size)
    }
    return size
  }

  if (typeId === TYPES.VARIANT) {
    const typeIndex = data[offset]
    const selected = typeDef.variants?.[typeIndex]
    return 1 + (selected ? getValueSize(selected.typeDef, data, offset + 1) : 0)
  }

  if (typeId === TYPES.RESOURCE) {
    const [count, countBytes] = decodeVarint(data, offset)
    let size = countBytes
    for (let i = 0; i < count; i++) {
      const [, idBytes] = decodeVarint(data, offset + size)
      size += idBytes
      const [, verBytes] = decodeVarint(data, offset + size)
      size += verBytes
      const [, sizeBytes] = decodeVarint(data, offset + size)
      size += sizeBytes
      size += getValueSize(typeDef.headerTypeDef!, data, offset + size)
    }
    return size
  }

  return getTypeSize(typeId)
}
