import { TYPES } from './constants.js'

/** Get the wire size in bytes for a basic type ID. Returns 0 for unknown types. */
export function getTypeSize(typeId: number): number {
  switch (typeId) {
    case TYPES.BOOL:
    case TYPES.INT8:
    case TYPES.UINT8:
      return 1
    case TYPES.INT16:
    case TYPES.UINT16:
      return 2
    case TYPES.INT32:
    case TYPES.FLOAT32:
      return 4
    default:
      return 0
  }
}

/** Encode a basic type value into a DataView at offset. All multi-byte values are little-endian. */
export function encodeValue(view: DataView, offset: number, value: any, typeId: number): void {
  switch (typeId) {
    case TYPES.BOOL:
      view.setUint8(offset, value ? 1 : 0)
      break
    case TYPES.INT8:
      view.setInt8(offset, value)
      break
    case TYPES.UINT8:
      view.setUint8(offset, value)
      break
    case TYPES.INT16:
      view.setInt16(offset, value, true)
      break
    case TYPES.UINT16:
      view.setUint16(offset, value, true)
      break
    case TYPES.INT32:
      view.setInt32(offset, value, true)
      break
    case TYPES.FLOAT32:
      view.setFloat32(offset, value, true)
      break
  }
}

/** Decode a basic type value from a DataView at offset. Returns [value, bytesRead]. */
export function decodeValue(view: DataView, offset: number, typeId: number): [any, number] {
  switch (typeId) {
    case TYPES.BOOL:
      return [!!view.getUint8(offset), 1]
    case TYPES.INT8:
      return [view.getInt8(offset), 1]
    case TYPES.UINT8:
      return [view.getUint8(offset), 1]
    case TYPES.INT16:
      return [view.getInt16(offset, true), 2]
    case TYPES.UINT16:
      return [view.getUint16(offset, true), 2]
    case TYPES.INT32:
      return [view.getInt32(offset, true), 4]
    case TYPES.FLOAT32:
      return [view.getFloat32(offset, true), 4]
    default:
      return [null, 0]
  }
}
