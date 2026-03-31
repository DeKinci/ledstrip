import { OPCODES } from '../wire/constants.js'
import { encodeVarint } from '../wire/varint.js'

/** Encode a PING request with a varint payload. */
export function encodePing(payload: number): ArrayBuffer {
  const payloadBytes = encodeVarint(payload)
  const buf = new ArrayBuffer(1 + payloadBytes.length)
  const bytes = new Uint8Array(buf)
  bytes[0] = OPCODES.PING
  bytes.set(payloadBytes, 1)
  return buf
}

/** Encode a PONG response (echo the original PING data with is_response flag set). */
export function encodePong(originalData: Uint8Array): ArrayBuffer {
  const buf = new ArrayBuffer(originalData.length)
  const bytes = new Uint8Array(buf)
  bytes.set(originalData)
  bytes[0] = OPCODES.PING | 0x10 // set is_response flag (bit 0 in flags = bit 4 in header)
  return buf
}
