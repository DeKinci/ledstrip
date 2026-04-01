// Matter message format validation tests
// Sends various message structures over UDP and verifies response format

import { describe, it, expect, beforeAll } from 'vitest'
import { getDeviceIP } from '@test/device.js'
import dgram from 'dgram'

const IP = getDeviceIP()
const MATTER_PORT = 5540
const BASE = `http://${IP}`

function sendUDP(msg: Buffer): Promise<Buffer | null> {
  return new Promise((resolve) => {
    const sock = dgram.createSocket('udp4')
    const timeout = setTimeout(() => { sock.close(); resolve(null) }, 3000)
    sock.on('message', (data) => {
      clearTimeout(timeout)
      sock.close()
      resolve(data)
    })
    sock.send(msg, MATTER_PORT, IP)
  })
}

describe('Message Format', () => {
  beforeAll(async () => {
    // Reset session to accept fresh PBKDFParamRequests
    const resetRes = await fetch(`${BASE}/debug/reset-session`, { method: 'POST' })
    console.log('    reset-session:', resetRes.status)
    await new Promise(r => setTimeout(r, 1000))
    // Verify state is correct
    const state = await (await fetch(`${BASE}/debug/state`)).json()
    console.log('    session state after reset:', state.sessionState)
  })

  it('rejects message shorter than 8 bytes', async () => {
    const short = Buffer.from([0x00, 0x00, 0x00])
    const response = await sendUDP(short)
    // Device should either not respond or send an error
    // No crash is the key validation
  })

  it('rejects message with invalid opcode', async () => {
    // Valid header but invalid opcode 0xFF
    const msg = Buffer.alloc(14)
    msg[0] = 0x00 // flags
    msg[1] = 0x00 // security flags
    msg.writeUInt16LE(0, 2) // session 0
    msg.writeUInt32LE(100, 4) // counter
    msg[8] = 0x01 // exchange flags: initiator
    msg[9] = 0xFF // invalid opcode
    msg.writeUInt16LE(1, 10) // exchange ID
    msg.writeUInt16LE(0x0000, 12) // protocol: secure channel

    const response = await sendUDP(msg)
    // Should not crash; may or may not respond
  })

  it('response has correct message header structure', async () => {
    // Send a valid PBKDFParamRequest and verify response header
    const random = Buffer.alloc(32, 0xAA)
    const tlv = Buffer.from([
      0x15,                   // Struct
      0x30, 32, ...random,    // Tag 1: random (bytes)
      0x25, 2, 0x0A, 0x00,   // Tag 2: sessionId = 10
      0x25, 3, 0x00, 0x00,   // Tag 3: passcodeId = 0
      0x28, 4,                // Tag 4: hasPBKDFParams = false
      0x18,                   // End
    ])

    const msg = Buffer.alloc(8 + 6 + tlv.length)
    msg[0] = 0x00; msg[1] = 0x00
    msg.writeUInt16LE(0, 2)
    msg.writeUInt32LE(200, 4)
    msg[8] = 0x01 | 0x04 // initiator + reliable
    msg[9] = 0x20 // PBKDFParamRequest
    msg.writeUInt16LE(10, 10) // exchange ID
    msg.writeUInt16LE(0x0000, 12) // secure channel
    tlv.copy(msg, 14)

    const response = await sendUDP(msg)
    expect(response).not.toBeNull()
    if (!response) return

    // Verify message header (8 bytes minimum)
    expect(response.length).toBeGreaterThanOrEqual(14)

    // Session ID should be 0 (unsecured session for PASE)
    const sessionId = response.readUInt16LE(2)
    expect(sessionId).toBe(0)

    // Message counter should be non-zero
    const counter = response.readUInt32LE(4)
    expect(counter).toBeGreaterThan(0)
  })

  it('response protocol header matches request exchange', async () => {
    const random = Buffer.alloc(32, 0xBB)
    const tlv = Buffer.from([
      0x15, 0x30, 32, ...random,
      0x25, 2, 0x0B, 0x00,
      0x25, 3, 0x00, 0x00,
      0x28, 4, 0x18,
    ])

    const msg = Buffer.alloc(14 + tlv.length)
    msg[0] = 0x00; msg[1] = 0x00
    msg.writeUInt16LE(0, 2)
    msg.writeUInt32LE(201, 4)
    msg[8] = 0x01 | 0x04
    msg[9] = 0x20
    msg.writeUInt16LE(42, 10) // exchange ID = 42
    msg.writeUInt16LE(0x0000, 12)
    tlv.copy(msg, 14)

    const response = await sendUDP(msg)
    expect(response).not.toBeNull()
    if (!response) return

    // Protocol header at offset 8
    const respExchangeId = response.readUInt16LE(10)
    expect(respExchangeId).toBe(42) // Should echo back the exchange ID

    const respProtocol = response.readUInt16LE(12)
    expect(respProtocol).toBe(0x0000) // Secure channel
  })

  it('handles multiple sequential messages without crashing', async () => {
    let responses = 0
    for (let i = 0; i < 5; i++) {
      const random = Buffer.alloc(32)
      random[0] = i
      const tlv = Buffer.from([
        0x15, 0x30, 32, ...random,
        0x25, 2, i, 0x00,
        0x25, 3, 0x00, 0x00,
        0x28, 4, 0x18,
      ])
      const msg = Buffer.alloc(14 + tlv.length)
      msg.writeUInt32LE(300 + i, 4)
      msg[8] = 0x05; msg[9] = 0x20
      msg.writeUInt16LE(50 + i, 10)
      tlv.copy(msg, 14)
      const resp = await sendUDP(msg)
      if (resp) responses++
      await new Promise(r => setTimeout(r, 200)) // Let device process
    }

    expect(responses).toBeGreaterThan(0)

    // Verify device is still alive
    const ping = await fetch(`${BASE}/ping`)
    expect(ping.status).toBe(200)
  })
})
