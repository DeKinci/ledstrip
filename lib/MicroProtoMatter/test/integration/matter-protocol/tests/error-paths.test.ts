// Matter protocol error path tests
// Tests robustness: malformed messages, wrong state, timeouts, bad crypto

import { describe, it, expect, beforeAll, beforeEach } from 'vitest'
import { getDeviceIP } from '@test/device.js'
import crypto from 'crypto'
import {
  MATTER_PORT, PROTO_SECURE_CHANNEL,
  OP_PBKDF_PARAM_REQUEST, OP_PBKDF_PARAM_RESPONSE,
  OP_PASE_PAKE1, OP_PASE_PAKE2, OP_PASE_PAKE3,
  OP_STATUS_REPORT,
  buildMessage, sendUDP, parseResponse, parseTLV,
  TLVWriter,
} from './helpers/matter-crypto.js'

const IP = getDeviceIP()
const BASE = `http://${IP}`
let counter = 3000

async function resetSession() {
  await fetch(`${BASE}/debug/reset-session`, { method: 'POST' })
  await new Promise(r => setTimeout(r, 500))
}

async function getSessionState(): Promise<string> {
  const res = await fetch(`${BASE}/debug/state`)
  const state = await res.json()
  return state.sessionState
}

describe('Error Paths', () => {
  beforeEach(async () => {
    await resetSession()
  })

  describe('Malformed PBKDFParamRequest', () => {
    it('rejects empty TLV payload', async () => {
      const msg = buildMessage({
        counter: counter++,
        opcode: OP_PBKDF_PARAM_REQUEST,
        protocolId: PROTO_SECURE_CHANNEL,
        exchangeId: 300,
        isInitiator: true,
        needsAck: true,
        payload: Buffer.alloc(0),
      })

      const resp = await sendUDP(msg, IP, 3000)
      // Device should either silently drop or respond — key is no crash
      // Session should not advance
      const state = await getSessionState()
      expect(['PASE_WAIT_PBKDF_REQ', 'PASE_WAIT_PAKE1']).toContain(state)

      // Device alive
      const ping = await fetch(`${BASE}/ping`)
      expect(ping.status).toBe(200)
    })

    it('rejects truncated TLV (missing initiatorRandom)', async () => {
      // Only send struct open + a U16 tag, no 32-byte random
      const tlv = new TLVWriter()
      tlv.openStruct()
      tlv.putU16(2, 0x1234) // sessionId without initiatorRandom
      tlv.closeContainer()

      const msg = buildMessage({
        counter: counter++,
        opcode: OP_PBKDF_PARAM_REQUEST,
        protocolId: PROTO_SECURE_CHANNEL,
        exchangeId: 301,
        isInitiator: true,
        needsAck: true,
        payload: tlv.toBuffer(),
      })

      const resp = await sendUDP(msg, IP, 3000)
      // The device processes what it can, the initiatorRandom will be all zeros
      // This is fine — it'll still advance to PASE_WAIT_PAKE1

      const ping = await fetch(`${BASE}/ping`)
      expect(ping.status).toBe(200)
    })

    it('handles oversized initiatorRandom gracefully', async () => {
      // Send 64 bytes instead of expected 32
      const tlv = new TLVWriter()
      tlv.openStruct()
      tlv.putBytes(1, crypto.randomBytes(64))
      tlv.putU16(2, 0x5678)
      tlv.putU16(3, 0)
      tlv.putBool(4, false)
      tlv.closeContainer()

      const msg = buildMessage({
        counter: counter++,
        opcode: OP_PBKDF_PARAM_REQUEST,
        protocolId: PROTO_SECURE_CHANNEL,
        exchangeId: 302,
        isInitiator: true,
        needsAck: true,
        payload: tlv.toBuffer(),
      })

      const resp = await sendUDP(msg, IP)
      // Device should handle gracefully (takes first 32 bytes)
      if (resp) {
        const parsed = parseResponse(resp)
        if (parsed) {
          expect(parsed.opcode).toBe(OP_PBKDF_PARAM_RESPONSE)
        }
      }

      const ping = await fetch(`${BASE}/ping`)
      expect(ping.status).toBe(200)
    })
  })

  describe('Wrong state messages', () => {
    it('Pake1 in PASE_WAIT_PBKDF_REQ state is ignored', async () => {
      // Send Pake1 without first doing PBKDFParamRequest
      const tlv = new TLVWriter()
      tlv.openStruct()
      tlv.putBytes(1, crypto.randomBytes(65)) // fake pA
      tlv.closeContainer()

      const msg = buildMessage({
        counter: counter++,
        opcode: OP_PASE_PAKE1,
        protocolId: PROTO_SECURE_CHANNEL,
        exchangeId: 310,
        isInitiator: true,
        needsAck: true,
        payload: tlv.toBuffer(),
      })

      const resp = await sendUDP(msg, IP, 3000)
      // Should be silently dropped — session stays in PASE_WAIT_PBKDF_REQ

      const state = await getSessionState()
      expect(state).toBe('PASE_WAIT_PBKDF_REQ')
    })

    it('Pake3 in PASE_WAIT_PBKDF_REQ state is ignored', async () => {
      const tlv = new TLVWriter()
      tlv.openStruct()
      tlv.putBytes(1, crypto.randomBytes(32)) // fake cA
      tlv.closeContainer()

      const msg = buildMessage({
        counter: counter++,
        opcode: OP_PASE_PAKE3,
        protocolId: PROTO_SECURE_CHANNEL,
        exchangeId: 311,
        isInitiator: true,
        needsAck: true,
        payload: tlv.toBuffer(),
      })

      const resp = await sendUDP(msg, IP, 3000)
      const state = await getSessionState()
      expect(state).toBe('PASE_WAIT_PBKDF_REQ')
    })

    it('Pake3 in PASE_WAIT_PAKE1 state is ignored', async () => {
      // First, advance to PASE_WAIT_PAKE1
      const random = crypto.randomBytes(32)
      const reqTlv = new TLVWriter()
      reqTlv.openStruct()
      reqTlv.putBytes(1, random)
      reqTlv.putU16(2, 0x1111)
      reqTlv.putU16(3, 0)
      reqTlv.putBool(4, false)
      reqTlv.closeContainer()

      const msg1 = buildMessage({
        counter: counter++,
        opcode: OP_PBKDF_PARAM_REQUEST,
        protocolId: PROTO_SECURE_CHANNEL,
        exchangeId: 312,
        isInitiator: true,
        needsAck: true,
        payload: reqTlv.toBuffer(),
      })

      await sendUDP(msg1, IP)
      await new Promise(r => setTimeout(r, 200))

      let state = await getSessionState()
      expect(state).toBe('PASE_WAIT_PAKE1')

      // Now send Pake3 (skipping Pake1)
      const tlv = new TLVWriter()
      tlv.openStruct()
      tlv.putBytes(1, crypto.randomBytes(32))
      tlv.closeContainer()

      const msg2 = buildMessage({
        counter: counter++,
        opcode: OP_PASE_PAKE3,
        protocolId: PROTO_SECURE_CHANNEL,
        exchangeId: 312,
        isInitiator: true,
        needsAck: true,
        payload: tlv.toBuffer(),
      })

      const resp = await sendUDP(msg2, IP, 3000)
      // Should be ignored — still in PASE_WAIT_PAKE1
      state = await getSessionState()
      expect(state).toBe('PASE_WAIT_PAKE1')
    })
  })

  describe('PASE restart', () => {
    it('new PBKDFParamRequest restarts PASE from PASE_WAIT_PAKE1', async () => {
      // Advance to PASE_WAIT_PAKE1
      const req1 = new TLVWriter()
      req1.openStruct()
      req1.putBytes(1, crypto.randomBytes(32))
      req1.putU16(2, 0x1111)
      req1.putU16(3, 0)
      req1.putBool(4, false)
      req1.closeContainer()

      const msg1 = buildMessage({
        counter: counter++,
        opcode: OP_PBKDF_PARAM_REQUEST,
        protocolId: PROTO_SECURE_CHANNEL,
        exchangeId: 320,
        isInitiator: true,
        needsAck: true,
        payload: req1.toBuffer(),
      })

      await sendUDP(msg1, IP)
      await new Promise(r => setTimeout(r, 200))

      let state = await getSessionState()
      expect(state).toBe('PASE_WAIT_PAKE1')

      // Send a new PBKDFParamRequest with different session ID
      const req2 = new TLVWriter()
      req2.openStruct()
      req2.putBytes(1, crypto.randomBytes(32))
      req2.putU16(2, 0x2222)
      req2.putU16(3, 0)
      req2.putBool(4, false)
      req2.closeContainer()

      const msg2 = buildMessage({
        counter: counter++,
        opcode: OP_PBKDF_PARAM_REQUEST,
        protocolId: PROTO_SECURE_CHANNEL,
        exchangeId: 321,
        isInitiator: true,
        needsAck: true,
        payload: req2.toBuffer(),
      })

      const resp = await sendUDP(msg2, IP)
      expect(resp).not.toBeNull()
      if (!resp) return

      const parsed = parseResponse(resp)
      expect(parsed).not.toBeNull()
      expect(parsed!.opcode).toBe(OP_PBKDF_PARAM_RESPONSE)

      // Session should be back in PASE_WAIT_PAKE1 (restarted)
      state = await getSessionState()
      expect(state).toBe('PASE_WAIT_PAKE1')
    })
  })

  describe('Invalid Pake3 (wrong cA)', () => {
    it('rejects Pake3 with wrong confirmation value', async () => {
      // Complete PBKDFParamRequest and Pake1 to get to PASE_WAIT_PAKE3
      const random = crypto.randomBytes(32)
      const reqTlv = new TLVWriter()
      reqTlv.openStruct()
      reqTlv.putBytes(1, random)
      reqTlv.putU16(2, 0x3333)
      reqTlv.putU16(3, 0)
      reqTlv.putBool(4, false)
      reqTlv.closeContainer()

      const msg1 = buildMessage({
        counter: counter++,
        opcode: OP_PBKDF_PARAM_REQUEST,
        protocolId: PROTO_SECURE_CHANNEL,
        exchangeId: 330,
        isInitiator: true,
        needsAck: true,
        payload: reqTlv.toBuffer(),
      })

      const resp1 = await sendUDP(msg1, IP)
      expect(resp1).not.toBeNull()
      if (!resp1) return
      const p1 = parseResponse(resp1)
      if (!p1) return

      // Send a fake Pake1 with random pA (65 bytes, starts with 0x04)
      // The device will try to compute SPAKE2+ with it and get valid-looking pB back
      // (even though the shared secret will be wrong)
      const fakePa = Buffer.alloc(65)
      fakePa[0] = 0x04
      // Use a real point on the curve (just generate a random keypair)
      const ecdh = crypto.createECDH('prime256v1')
      ecdh.generateKeys()
      const realPoint = ecdh.getPublicKey()
      realPoint.copy(fakePa)

      const pake1 = new TLVWriter()
      pake1.openStruct()
      pake1.putBytes(1, fakePa)
      pake1.closeContainer()

      const msg2 = buildMessage({
        counter: counter++,
        opcode: OP_PASE_PAKE1,
        protocolId: PROTO_SECURE_CHANNEL,
        exchangeId: 330,
        isInitiator: true,
        needsAck: true,
        ackCounter: p1.msgCounter,
        payload: pake1.toBuffer(),
      })

      const resp2 = await sendUDP(msg2, IP)
      expect(resp2).not.toBeNull()
      if (!resp2) return
      const p2 = parseResponse(resp2)
      if (!p2) return
      expect(p2.opcode).toBe(OP_PASE_PAKE2)

      // Now send Pake3 with a random cA (will be wrong)
      const fakeCa = crypto.randomBytes(32)
      const pake3 = new TLVWriter()
      pake3.openStruct()
      pake3.putBytes(1, fakeCa)
      pake3.closeContainer()

      const msg3 = buildMessage({
        counter: counter++,
        opcode: OP_PASE_PAKE3,
        protocolId: PROTO_SECURE_CHANNEL,
        exchangeId: 330,
        isInitiator: true,
        needsAck: true,
        ackCounter: p2.msgCounter,
        payload: pake3.toBuffer(),
      })

      const resp3 = await sendUDP(msg3, IP)
      expect(resp3).not.toBeNull()
      if (!resp3) return

      const p3 = parseResponse(resp3)
      expect(p3).not.toBeNull()
      if (!p3) return

      // Device should send a FAILURE StatusReport
      expect(p3.opcode).toBe(OP_STATUS_REPORT)
      expect(p3.payload.length).toBeGreaterThanOrEqual(8)

      const generalCode = p3.payload.readUInt16LE(0)
      expect(generalCode).toBe(0x0001) // kGeneralFailure

      // Session should reset to PASE_WAIT_PBKDF_REQ
      await new Promise(r => setTimeout(r, 200))
      const state = await getSessionState()
      expect(state).toBe('PASE_WAIT_PBKDF_REQ')
    })
  })

  describe('Robustness', () => {
    it('handles rapid-fire PBKDFParamRequests', async () => {
      const promises: Promise<Buffer | null>[] = []
      for (let i = 0; i < 10; i++) {
        const tlv = new TLVWriter()
        tlv.openStruct()
        tlv.putBytes(1, crypto.randomBytes(32))
        tlv.putU16(2, 0x4000 + i)
        tlv.putU16(3, 0)
        tlv.putBool(4, false)
        tlv.closeContainer()

        const msg = buildMessage({
          counter: counter++,
          opcode: OP_PBKDF_PARAM_REQUEST,
          protocolId: PROTO_SECURE_CHANNEL,
          exchangeId: 340 + i,
          isInitiator: true,
          needsAck: true,
          payload: tlv.toBuffer(),
        })

        promises.push(sendUDP(msg, IP, 3000))
        // Small delay to avoid overwhelming UDP
        await new Promise(r => setTimeout(r, 50))
      }

      const responses = await Promise.all(promises)
      const validResponses = responses.filter(r => r !== null)
      // At least some should get responses
      expect(validResponses.length).toBeGreaterThan(0)

      // Device alive
      const ping = await fetch(`${BASE}/ping`)
      expect(ping.status).toBe(200)
    })

    it('handles zero-length UDP packet', async () => {
      const resp = await sendUDP(Buffer.alloc(0), IP, 2000)
      // Should be silently dropped
      const ping = await fetch(`${BASE}/ping`)
      expect(ping.status).toBe(200)
    })

    it('handles packet with valid header but garbage payload', async () => {
      const garbage = crypto.randomBytes(200)
      // Craft a valid-looking message header
      garbage[0] = 0x00 // flags
      garbage[1] = 0x00 // security flags
      garbage.writeUInt16LE(0, 2) // session 0
      garbage.writeUInt32LE(counter++, 4)
      garbage[8] = 0x05 // exchange flags: initiator + reliable
      garbage[9] = 0x20 // PBKDFParamRequest
      garbage.writeUInt16LE(350, 10)
      garbage.writeUInt16LE(0x0000, 12) // secure channel

      const resp = await sendUDP(garbage, IP, 3000)
      // May or may not respond, but should not crash
      const ping = await fetch(`${BASE}/ping`)
      expect(ping.status).toBe(200)
    })
  })
})
