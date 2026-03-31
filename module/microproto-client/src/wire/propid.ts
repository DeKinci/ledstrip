/** Encode a property/function ID (0-32767). Returns 1 byte for 0-127, 2 bytes for 128+. */
export function encodePropId(id: number): Uint8Array {
  if (id < 128) {
    return new Uint8Array([id])
  }
  return new Uint8Array([0x80 | (id & 0x7f), id >> 7])
}

/** Decode a property/function ID. Returns [id, bytesConsumed]. */
export function decodePropId(data: Uint8Array, offset: number): [number, number] {
  let propId = data[offset++]
  if (propId & 0x80) {
    propId = (propId & 0x7f) | (data[offset++] << 7)
    return [propId, 2]
  }
  return [propId, 1]
}
