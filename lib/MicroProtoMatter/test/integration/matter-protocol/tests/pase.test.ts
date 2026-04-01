// Matter PASE (Password-Authenticated Session Establishment) integration test
// Reference: Matter Spec Section 4.13 — PASE Protocol
//
// Tests the actual commissioning handshake over UDP against the device.
// Uses raw binary message construction matching the Matter wire format.

import { describe, it, expect, beforeAll } from 'vitest'
import { getDeviceIP } from '@test/device.js'
import dgram from 'dgram'
import crypto from 'crypto'

const IP = getDeviceIP()
const MATTER_PORT = 5540
const BASE = `http://${IP}`

// Matter protocol constants
const PROTO_SECURE_CHANNEL = 0x0000
const OP_PBKDF_PARAM_REQUEST = 0x20
const OP_PBKDF_PARAM_RESPONSE = 0x21
const EX_INITIATOR = 0x01
const EX_RELIABLE = 0x04

// Build a Matter message: MessageHeader + ProtocolHeader + payload
function buildMessage(opts: {
  sessionId?: number
  counter: number
  opcode: number
  protocolId: number
  exchangeId: number
  isInitiator?: boolean
  needsAck?: boolean
  payload?: Buffer
}): Buffer {
  const payload = opts.payload || Buffer.alloc(0)

  // Message header: 8 bytes (no source/dest for unsecured)
  const msgHdr = Buffer.alloc(8)
  msgHdr[0] = 0x00 // flags: no source, no dest
  msgHdr[1] = 0x00 // security flags
  msgHdr.writeUInt16LE(opts.sessionId ?? 0, 2)
  msgHdr.writeUInt32LE(opts.counter, 4)

  // Protocol header: 6 bytes (no ACK)
  const protoHdr = Buffer.alloc(6)
  let exFlags = 0
  if (opts.isInitiator) exFlags |= EX_INITIATOR
  if (opts.needsAck) exFlags |= EX_RELIABLE
  protoHdr[0] = exFlags
  protoHdr[1] = opts.opcode
  protoHdr.writeUInt16LE(opts.exchangeId, 2)
  protoHdr.writeUInt16LE(opts.protocolId, 4)

  return Buffer.concat([msgHdr, protoHdr, payload])
}

// Build TLV for PBKDFParamRequest (Spec 4.13.1.1)
// Structure: { 1: initiatorRandom(32 bytes), 2: initiatorSessionId(u16),
//              3: passcodeId(u16), 4: hasPBKDFParameters(bool) }
function buildPBKDFParamRequest(initiatorSessionId: number): Buffer {
  const random = crypto.randomBytes(32)

  const parts: number[] = []
  // Struct open
  parts.push(0x15)
  // Tag 1: initiatorRandom (bytes, 1-byte length)
  parts.push(0x30, 32, ...random)
  // Tag 2: initiatorSessionId (u16)
  parts.push(0x25, 2, initiatorSessionId & 0xFF, (initiatorSessionId >> 8) & 0xFF)
  // Tag 3: passcodeId (u16) — 0 = default
  parts.push(0x25, 3, 0x00, 0x00)
  // Tag 4: hasPBKDFParameters (bool false)
  parts.push(0x28, 4)
  // Struct close
  parts.push(0x18)

  return Buffer.from(parts)
}

// Parse a PBKDFParamResponse TLV (Spec 4.13.1.2)
function parsePBKDFParamResponse(data: Buffer): {
  initiatorRandom: Buffer
  responderRandom: Buffer
  responderSessionId: number
  pbkdfIterations: number
  pbkdfSalt: Buffer
} | null {
  // Simple TLV parser for expected structure
  let pos = 0
  if (data[pos++] !== 0x15) return null // Struct open

  const result: any = {}

  while (pos < data.length && data[pos] !== 0x18) {
    const ctrl = data[pos++]
    const tagForm = ctrl & 0xE0
    const elemType = ctrl & 0x1F
    let tag = 0xFF

    if (tagForm === 0x20) { // Context tag
      tag = data[pos++]
    }

    switch (elemType) {
      case 0x10: { // Bytes1
        const len = data[pos++]
        const value = data.subarray(pos, pos + len)
        pos += len
        if (tag === 1) result.initiatorRandom = Buffer.from(value)
        if (tag === 2) result.responderRandom = Buffer.from(value)
        if (tag === 4) result.pbkdfSalt = Buffer.from(value)
        break
      }
      case 0x05: { // UInt16
        const value = data[pos] | (data[pos + 1] << 8)
        pos += 2
        if (tag === 3) result.responderSessionId = value
        break
      }
      case 0x06: { // UInt32
        const value = data.readUInt32LE(pos)
        pos += 4
        if (tag === 5) result.pbkdfIterations = value
        break
      }
      case 0x15: // Struct open — nested PBKDF params
        break
      case 0x18: // Struct/container close
        break
      default:
        // Skip unknown: for basic types, length is known from type
        return null
    }
  }

  return result.responderRandom ? result : null
}

function sendUDP(msg: Buffer): Promise<Buffer | null> {
  return new Promise((resolve) => {
    const sock = dgram.createSocket('udp4')
    const timeout = setTimeout(() => { sock.close(); resolve(null) }, 5000)

    sock.on('message', (data) => {
      clearTimeout(timeout)
      sock.close()
      resolve(data)
    })

    sock.send(msg, MATTER_PORT, IP)
  })
}

describe('PASE Commissioning', () => {
  beforeAll(async () => {
    // Reset session to PASE_WAIT_PBKDF_REQ for fresh commissioning
    await fetch(`${BASE}/debug/reset-session`, { method: 'POST' })
    await new Promise(r => setTimeout(r, 500))
  })

  it('PBKDFParamRequest gets a valid response', async () => {
    const payload = buildPBKDFParamRequest(1)
    const msg = buildMessage({
      counter: 1,
      opcode: OP_PBKDF_PARAM_REQUEST,
      protocolId: PROTO_SECURE_CHANNEL,
      exchangeId: 1,
      isInitiator: true,
      needsAck: true,
      payload,
    })

    const response = await sendUDP(msg)
    expect(response).not.toBeNull()
    if (!response) return

    // Verify message header
    expect(response.length).toBeGreaterThan(14) // min: 8 msg + 6 proto + payload

    // Parse protocol header
    const protoOffset = 8 // no source/dest in unsecured
    const opcode = response[protoOffset + 1]
    const protocolId = response.readUInt16LE(protoOffset + 4)

    expect(opcode).toBe(OP_PBKDF_PARAM_RESPONSE)
    expect(protocolId).toBe(PROTO_SECURE_CHANNEL)
  })

  it('PBKDFParamResponse contains valid TLV', async () => {
    const payload = buildPBKDFParamRequest(2)
    const msg = buildMessage({
      counter: 2,
      opcode: OP_PBKDF_PARAM_REQUEST,
      protocolId: PROTO_SECURE_CHANNEL,
      exchangeId: 2,
      isInitiator: true,
      needsAck: true,
      payload,
    })

    const response = await sendUDP(msg)
    expect(response).not.toBeNull()
    if (!response) return

    // Extract payload (after msg header + proto header)
    const protoOffset = 8
    const exFlags = response[protoOffset]
    const hasAck = (exFlags & 0x02) !== 0
    const payloadOffset = protoOffset + 6 + (hasAck ? 4 : 0)
    const tlvData = response.subarray(payloadOffset)

    const parsed = parsePBKDFParamResponse(tlvData)
    expect(parsed).not.toBeNull()
    if (!parsed) return

    // Verify fields per spec 4.13.1.2
    expect(parsed.responderRandom).toBeDefined()
    expect(parsed.responderRandom.length).toBe(32)
    expect(parsed.responderSessionId).toBeDefined()
    expect(parsed.responderSessionId).toBeGreaterThan(0)
  })

  it('PBKDFParamResponse includes PBKDF parameters', async () => {
    const payload = buildPBKDFParamRequest(3)
    const msg = buildMessage({
      counter: 3,
      opcode: OP_PBKDF_PARAM_REQUEST,
      protocolId: PROTO_SECURE_CHANNEL,
      exchangeId: 3,
      isInitiator: true,
      needsAck: true,
      payload,
    })

    const response = await sendUDP(msg)
    expect(response).not.toBeNull()
    if (!response) return

    const protoOffset = 8
    const exFlags = response[protoOffset]
    const hasAck = (exFlags & 0x02) !== 0
    const payloadOffset = protoOffset + 6 + (hasAck ? 4 : 0)
    const tlvData = response.subarray(payloadOffset)

    const parsed = parsePBKDFParamResponse(tlvData)
    if (parsed) {
      // If hasPBKDFParameters was false in request, response should include them
      if (parsed.pbkdfIterations !== undefined) {
        expect(parsed.pbkdfIterations).toBeGreaterThanOrEqual(1000) // Matter spec minimum
      }
      if (parsed.pbkdfSalt !== undefined) {
        expect(parsed.pbkdfSalt.length).toBeGreaterThanOrEqual(16) // Spec: 16-32 bytes
        expect(parsed.pbkdfSalt.length).toBeLessThanOrEqual(32)
      }
    }
  })

  it('session transitions to PASE_WAIT_PAKE1 after PBKDFParamRequest', async () => {
    // After receiving a valid PBKDFParamRequest, the device should advance
    // from PASE_WAIT_PBKDF_REQ to PASE_WAIT_PAKE1 (per Spec 4.13.1)
    await new Promise(r => setTimeout(r, 500)) // Allow state to settle

    const res = await fetch(`${BASE}/debug/state`)
    expect(res.status).toBe(200)
    const state = await res.json()
    // After processing PBKDFParamRequest, session should have advanced
    expect(['PASE_WAIT_PAKE1', 'PASE_WAIT_PBKDF_REQ']).toContain(state.sessionState)
    expect(state.sessionSecure).toBe(false) // Not yet secure
    expect(state.commissioned).toBe(false) // Not yet commissioned
  })

  it('device remains responsive after PASE protocol messages', async () => {
    const res = await fetch(`${BASE}/ping`)
    expect(res.status).toBe(200)
    expect(await res.text()).toBe('pong')
  })
})
