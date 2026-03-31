import { decodeVarint } from '../wire/varint.js'

export interface ErrorMessage {
  code: number
  message: string
  schemaMismatch: boolean
}

/** Decode an ERROR message. */
export function decodeError(data: Uint8Array, flags: number): ErrorMessage {
  const schemaMismatch = !!(flags & 0x01)
  const view = new DataView(data.buffer, data.byteOffset)

  const code = view.getUint16(1, true)
  const [msgLen, varintBytes] = decodeVarint(data, 3)
  const message = new TextDecoder().decode(data.slice(3 + varintBytes, 3 + varintBytes + msgLen))

  return { code, message, schemaMismatch }
}
