/** Encode a uint32 as a varint. Returns a Uint8Array. */
export function encodeVarint(value: number): Uint8Array {
  const bytes: number[] = []
  while (value > 0x7f) {
    bytes.push(0x80 | (value & 0x7f))
    value >>>= 7
  }
  bytes.push(value & 0x7f)
  return new Uint8Array(bytes)
}

/** Decode a varint from data at offset. Returns [value, bytesConsumed]. */
export function decodeVarint(data: Uint8Array, offset: number): [number, number] {
  let result = 0
  let shift = 0
  let bytesConsumed = 0

  while (offset < data.length) {
    const byte = data[offset++]
    bytesConsumed++
    result |= (byte & 0x7f) << shift
    if ((byte & 0x80) === 0) break
    shift += 7
    if (bytesConsumed > 5) break
  }

  return [result >>> 0, bytesConsumed]
}

/** Calculate the byte size of a varint encoding for a given value. */
export function varintSize(value: number): number {
  if (value < 128) return 1
  if (value < 16384) return 2
  if (value < 2097152) return 3
  if (value < 268435456) return 4
  return 5
}
