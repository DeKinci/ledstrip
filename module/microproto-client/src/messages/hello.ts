import { OPCODES, PROTOCOL_VERSION } from '../wire/constants.js'
import { encodeVarint, decodeVarint } from '../wire/varint.js'

export interface HelloRequest {
  deviceId: number
  maxPacketSize: number
  schemaVersion: number
}

export interface HelloResponse {
  version: number
  maxPacket: number
  sessionId: number
  timestamp: number
  schemaVersion: number
}

/** Encode a HELLO request message. */
export function encodeHello(req: HelloRequest): ArrayBuffer {
  const maxPacketBytes = encodeVarint(req.maxPacketSize)
  const deviceIdBytes = encodeVarint(req.deviceId)

  const buf = new ArrayBuffer(2 + maxPacketBytes.length + deviceIdBytes.length + 2)
  const bytes = new Uint8Array(buf)

  let offset = 0
  bytes[offset++] = OPCODES.HELLO
  bytes[offset++] = PROTOCOL_VERSION
  bytes.set(maxPacketBytes, offset)
  offset += maxPacketBytes.length
  bytes.set(deviceIdBytes, offset)
  offset += deviceIdBytes.length
  bytes[offset++] = req.schemaVersion & 0xff
  bytes[offset++] = (req.schemaVersion >> 8) & 0xff

  return buf
}

/** Decode a HELLO response message. Returns null if too short. */
export function decodeHelloResponse(data: Uint8Array): HelloResponse | null {
  if (data.length < 4) return null

  let offset = 1 // skip header byte
  const version = data[offset++]

  const [maxPacket, mpBytes] = decodeVarint(data, offset)
  offset += mpBytes

  const [sessionId, sidBytes] = decodeVarint(data, offset)
  offset += sidBytes

  const [timestamp, tsBytes] = decodeVarint(data, offset)
  offset += tsBytes

  const schemaVersion = data[offset] | (data[offset + 1] << 8)

  return { version, maxPacket, sessionId, timestamp, schemaVersion }
}
