// Full PASE handshake integration test
// Tests the complete commissioning handshake: PBKDFParamRequest → Pake1 → Pake2 → Pake3 → Session
// Implements client-side SPAKE2+ to validate the device's crypto is correct end-to-end

import { describe, it, expect, beforeAll } from 'vitest'
import { getDeviceIP } from '@test/device.js'
import crypto from 'crypto'
import {
  MATTER_PORT, PROTO_SECURE_CHANNEL,
  OP_PBKDF_PARAM_REQUEST, OP_PBKDF_PARAM_RESPONSE,
  OP_PASE_PAKE1, OP_PASE_PAKE2, OP_PASE_PAKE3,
  OP_STATUS_REPORT,
  EX_INITIATOR, EX_RELIABLE,
  buildMessage, sendUDP, parseResponse, parseTLV,
  TLVWriter,
  spake2pDeriveW0W1, spake2pComputeProver, spake2pVerifyCb,
  deriveSessionKeys, type SessionKeys,
} from './helpers/matter-crypto.js'

const IP = getDeviceIP()
const BASE = `http://${IP}`
const TEST_PASSCODE = 20202021

let counter = 1000  // Start counters high to avoid collision with other test files
const exchangeId = 100

// Shared state across the handshake
let initiatorSessionId: number
let responderSessionId: number
let w0: Buffer, w1: Buffer
let pbkdfSalt: Buffer
let pbkdfIterations: number
let reqTLV: Buffer   // PBKDFParamRequest TLV (for context hash)
let respTLV: Buffer  // PBKDFParamResponse TLV (for context hash)
let lastAckCounter: number
let sessionKeys: SessionKeys

describe('Full PASE Handshake', () => {
  beforeAll(async () => {
    await fetch(`${BASE}/debug/reset-session`, { method: 'POST' })
    await new Promise(r => setTimeout(r, 1000))
  })

  it('Step 1: PBKDFParamRequest → PBKDFParamResponse', async () => {
    initiatorSessionId = 0x1234

    // Build PBKDFParamRequest TLV
    const random = crypto.randomBytes(32)
    const tlv = new TLVWriter()
    tlv.openStruct()
    tlv.putBytes(1, random)
    tlv.putU16(2, initiatorSessionId)
    tlv.putU16(3, 0)       // passcodeId = 0 (default)
    tlv.putBool(4, false)  // hasPBKDFParameters = false
    tlv.closeContainer()
    reqTLV = tlv.toBuffer()

    const msg = buildMessage({
      counter: counter++,
      opcode: OP_PBKDF_PARAM_REQUEST,
      protocolId: PROTO_SECURE_CHANNEL,
      exchangeId,
      isInitiator: true,
      needsAck: true,
      payload: reqTLV,
    })

    const response = await sendUDP(msg, IP)
    expect(response).not.toBeNull()
    if (!response) return

    const parsed = parseResponse(response)
    expect(parsed).not.toBeNull()
    if (!parsed) return

    expect(parsed.opcode).toBe(OP_PBKDF_PARAM_RESPONSE)
    expect(parsed.protocolId).toBe(PROTO_SECURE_CHANNEL)
    lastAckCounter = parsed.msgCounter

    // Parse PBKDFParamResponse TLV
    respTLV = parsed.payload
    const fields = parseTLV(respTLV)

    // Tag 1: initiatorRandom (should echo back)
    const echoedRandom = fields.get(1)
    expect(echoedRandom).toBeDefined()
    expect(echoedRandom!.value.length).toBe(32)
    expect(Buffer.compare(echoedRandom!.value, random)).toBe(0)

    // Tag 2: responderRandom (32 bytes)
    const respRandom = fields.get(2)
    expect(respRandom).toBeDefined()
    expect(respRandom!.value.length).toBe(32)

    // Tag 3: responderSessionId
    const sessId = fields.get(3)
    expect(sessId).toBeDefined()
    responderSessionId = sessId!.value[0] | (sessId!.value[1] << 8)
    expect(responderSessionId).toBeGreaterThan(0)

    // PBKDF params are in a nested struct (tag 4)
    // Parse manually from the raw TLV
    const rawResp = respTLV
    let pos = 0
    if (rawResp[pos] === 0x15) pos++ // struct open

    // Walk through looking for the nested struct with iterations and salt
    pbkdfIterations = 1000
    pbkdfSalt = Buffer.alloc(32)

    // Simple scan for context tag 4 followed by struct
    for (let i = 0; i < rawResp.length - 2; i++) {
      // Look for context tag 4 + struct: 0x35 0x04 (kTagContext | kTLVStruct, tag=4)
      if (rawResp[i] === 0x35 && rawResp[i + 1] === 0x04) {
        // Inside PBKDF params struct
        let j = i + 2
        while (j < rawResp.length && rawResp[j] !== 0x18) {
          const ctrl = rawResp[j++]
          const tf = ctrl & 0xE0
          const et = ctrl & 0x1F
          let tag = 0xFF
          if (tf === 0x20) tag = rawResp[j++]

          if (tag === 1 && et === 0x06) {
            // iterations (u32)
            pbkdfIterations = rawResp[j] | (rawResp[j+1] << 8) |
                              (rawResp[j+2] << 16) | (rawResp[j+3] << 24)
            j += 4
          } else if (tag === 2 && et === 0x10) {
            // salt (bytes1)
            const len = rawResp[j++]
            pbkdfSalt = Buffer.from(rawResp.subarray(j, j + len))
            j += len
          } else {
            break
          }
        }
        break
      }
    }

    expect(pbkdfIterations).toBeGreaterThanOrEqual(1000)
    expect(pbkdfSalt.length).toBeGreaterThanOrEqual(16)

    // Derive w0, w1 from passcode
    const derived = spake2pDeriveW0W1(TEST_PASSCODE, pbkdfSalt, pbkdfIterations)
    w0 = derived.w0
    w1 = derived.w1
  })

  it('Step 2: Pake1 → Pake2 (SPAKE2+ key exchange)', async () => {
    expect(w0).toBeDefined()
    expect(w1).toBeDefined()
    expect(reqTLV).toBeDefined()
    expect(respTLV).toBeDefined()

    // Compute PASE context hash: SHA-256("CHIP PAKE V1 Commissioning" || req || resp)
    const prefix = Buffer.from('CHIP PAKE V1 Commissioning', 'ascii')
    const contextHash = crypto.createHash('sha256')
      .update(prefix)
      .update(reqTLV)
      .update(respTLV)
      .digest()

    // We need to send Pake1 and get Pake2 back, then verify
    // First, send a dummy Pake1 to get pB, then compute with it
    // Actually, we need to compute pA first (which doesn't depend on pB),
    // send it, get pB back, then compute the shared keys

    // Generate our random scalar x and compute pA = x*G + w0*M
    // We'll do this manually since spake2pComputeProver needs pB
    const x = crypto.randomBytes(32)
    // We need to compute pA without spake2pComputeProver
    // pA computation: generate x, compute x*G, add w0*M

    // Actually, spake2pComputeProver needs pB which we don't have yet.
    // The flow is:
    // 1. Compute pA = x*G + w0*M (doesn't need pB)
    // 2. Send Pake1 with pA
    // 3. Receive Pake2 with pB and cB
    // 4. Compute shared keys using pB
    // 5. Verify cB
    // 6. Compute cA and send Pake3

    // Let's restructure: compute pA first, then after getting pB, compute everything
    // We'll use the EC arithmetic from the helper

    // Import the EC functions we need
    const { ecScalarMulG, ecScalarMul, ecPointAdd, ecPointNegate, POINT_M, POINT_N,
            bigintToBuffer, bufferToBigint, P256_ORDER } = await import('./helpers/ec-math.js')
      .catch(() => {
        // ec-math not available, use the inline approach
        return null
      })

    // Actually, let's just use the spake2pComputeProver function in a two-phase approach
    // Phase 1: we need to generate pA independently
    // Phase 2: once we have pB, compute everything

    // The simplest approach: send Pake1 with a known pA, get pB, then compute keys
    // But we need the SAME x for both phases.

    // Let's restructure the helper to support this. For now, let's just test
    // that the device responds to Pake1 with a valid Pake2 message format.
    // The full crypto verification will be in the "verify session keys" test.

    // For a real E2E test, we need to implement the prover side properly.
    // Let's use the manual EC math from the helper module.

    // Generate x (random scalar)
    const xBig = BigInt('0x' + x.toString('hex')) % BigInt('0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551')
    const xBuf = Buffer.from(xBig.toString(16).padStart(64, '0'), 'hex')

    // X = x * G (use ECDH trick)
    const ecdh = crypto.createECDH('prime256v1')
    ecdh.setPrivateKey(xBuf)
    const xG = Buffer.from(ecdh.getPublicKey())

    // w0 * M: compute using our EC scalar multiplication
    // We need the full point, not just x-coordinate, so we can't use ECDH computeSecret
    // Let's use the affine coordinate math from the helper

    // Import the helper's EC functions directly
    const helperModule = await import('./helpers/matter-crypto.js')

    // For now, just verify that Pake1 gets a Pake2 response with correct structure
    // We'll send xG as pA (which is x*G, not x*G + w0*M) — the device will still
    // process it and respond, but the keys won't match. That's OK for format testing.
    // The full crypto test follows.

    // Actually, let's do this properly. We have EC point arithmetic in the helper.
    // Let me compute it step by step.

    // w0*M using double-and-add
    const { ecScalarMul: scalarMul, ecPointAdd: pointAdd } = await getECMath()
    const w0M = scalarMul(
      Buffer.from([0x04,
        0x88,0x6e,0x2f,0x97,0xac,0xe4,0x6e,0x55,0xba,0x9d,0xd7,0x24,0x25,0x79,0xf2,0x99,
        0x3b,0x64,0xe1,0x6e,0xf3,0xdc,0xab,0x95,0xaf,0xd4,0x97,0x33,0x3d,0x8f,0xa1,0x2f,
        0x5f,0xf3,0x55,0x16,0x3e,0x43,0xce,0x22,0x4e,0x0b,0x0e,0x65,0xff,0x02,0xac,0x8e,
        0x5c,0x7b,0xe0,0x94,0x19,0xc7,0x85,0xe0,0xca,0x54,0x7d,0x55,0xa1,0x2e,0x2d,0x20]),
      w0
    )
    const pA = pointAdd(xG, w0M)

    // Build Pake1 TLV: { 1: pA (65 bytes) }
    const pake1 = new TLVWriter()
    pake1.openStruct()
    pake1.putBytes(1, pA)
    pake1.closeContainer()

    const msg = buildMessage({
      counter: counter++,
      opcode: OP_PASE_PAKE1,
      protocolId: PROTO_SECURE_CHANNEL,
      exchangeId,
      isInitiator: true,
      needsAck: true,
      ackCounter: lastAckCounter,
      payload: pake1.toBuffer(),
    })

    const response = await sendUDP(msg, IP)
    expect(response).not.toBeNull()
    if (!response) return

    const parsed = parseResponse(response)
    expect(parsed).not.toBeNull()
    if (!parsed) return

    expect(parsed.opcode).toBe(OP_PASE_PAKE2)
    expect(parsed.protocolId).toBe(PROTO_SECURE_CHANNEL)
    lastAckCounter = parsed.msgCounter

    // Parse Pake2: { 1: pB (65 bytes), 2: cB (32 bytes) }
    const fields = parseTLV(parsed.payload)
    const pBField = fields.get(1)
    const cBField = fields.get(2)
    expect(pBField).toBeDefined()
    expect(pBField!.value.length).toBe(65)
    expect(cBField).toBeDefined()
    expect(cBField!.value.length).toBe(32)

    const pB = Buffer.from(pBField!.value)
    const cB = Buffer.from(cBField!.value)

    // Now compute the full SPAKE2+ prover side
    const { ecPointNegate: negate } = await getECMath()
    const POINT_N_BUF = Buffer.from([0x04,
      0xd8,0xbb,0xd6,0xc6,0x39,0xc6,0x29,0x37,0xb0,0x4d,0x99,0x7f,0x38,0xc3,0x77,0x07,
      0x19,0xc6,0x29,0xd7,0x01,0x4d,0x49,0xa2,0x4b,0x4f,0x98,0xba,0xa1,0x29,0x2b,0x49,
      0x07,0xd6,0x0a,0xa6,0xbf,0xad,0xe4,0x50,0x08,0xa6,0x36,0x33,0x7f,0x51,0x68,0xc6,
      0x4d,0x9b,0xd3,0x60,0x34,0x80,0x8c,0xd5,0x64,0x49,0x0b,0x1e,0x65,0x6e,0xdb,0xe7])

    // Y = pB - w0*N
    const w0N = scalarMul(POINT_N_BUF, w0)
    const Y = pointAdd(pB, negate(w0N))

    // Z = x * Y
    const Z = scalarMul(Y, xBuf)

    // V = x * L where L = w1 * G
    const L = (() => {
      const e = crypto.createECDH('prime256v1')
      e.setPrivateKey(w1)
      return Buffer.from(e.getPublicKey())
    })()
    const V = scalarMul(L, xBuf)

    // Build transcript hash TT
    const POINT_M_BUF = Buffer.from([0x04,
      0x88,0x6e,0x2f,0x97,0xac,0xe4,0x6e,0x55,0xba,0x9d,0xd7,0x24,0x25,0x79,0xf2,0x99,
      0x3b,0x64,0xe1,0x6e,0xf3,0xdc,0xab,0x95,0xaf,0xd4,0x97,0x33,0x3d,0x8f,0xa1,0x2f,
      0x5f,0xf3,0x55,0x16,0x3e,0x43,0xce,0x22,0x4e,0x0b,0x0e,0x65,0xff,0x02,0xac,0x8e,
      0x5c,0x7b,0xe0,0x94,0x19,0xc7,0x85,0xe0,0xca,0x54,0x7d,0x55,0xa1,0x2e,0x2d,0x20])

    const hash = crypto.createHash('sha256')
    function ttAppend(data: Buffer | null) {
      const lenBuf = Buffer.alloc(8)
      const len = data ? data.length : 0
      lenBuf.writeUInt32LE(len, 0)
      hash.update(lenBuf)
      if (data && len > 0) hash.update(data)
    }
    ttAppend(contextHash)
    ttAppend(null)        // A identity (empty)
    ttAppend(null)        // B identity (empty)
    ttAppend(POINT_M_BUF)
    ttAppend(POINT_N_BUF)
    ttAppend(pA)          // X = pA
    ttAppend(pB)          // Y = pB
    ttAppend(Z)
    ttAppend(V)
    ttAppend(w0)

    const hashTT = hash.digest()

    const Ka = hashTT.subarray(0, 16)
    const Ke = Buffer.from(hashTT.subarray(16, 32))

    // KcA || KcB = HKDF(salt=nil, IKM=Ka, info="ConfirmationKeys", L=32)
    const KcAKcB = Buffer.from(crypto.hkdfSync('sha256', Ka, Buffer.alloc(0), 'ConfirmationKeys', 32))
    const KcA = KcAKcB.subarray(0, 16)
    const KcB = KcAKcB.subarray(16, 32)

    // Verify cB = HMAC(KcB, pA)
    const expectedCB = crypto.createHmac('sha256', KcB).update(pA).digest()
    expect(Buffer.compare(expectedCB, cB)).toBe(0)

    // Compute cA = HMAC(KcA, pB)
    const cA = crypto.createHmac('sha256', KcA).update(pB).digest()

    // Store for next step
    ;(globalThis as any).__paseState = { Ke, cA, pA, pB }
  })

  it('Step 3: Pake3 → SessionEstablished', async () => {
    const state = (globalThis as any).__paseState
    expect(state).toBeDefined()
    if (!state) return

    const { Ke, cA } = state

    // Build Pake3 TLV: { 1: cA (32 bytes) }
    const pake3 = new TLVWriter()
    pake3.openStruct()
    pake3.putBytes(1, cA)
    pake3.closeContainer()

    const msg = buildMessage({
      counter: counter++,
      opcode: OP_PASE_PAKE3,
      protocolId: PROTO_SECURE_CHANNEL,
      exchangeId,
      isInitiator: true,
      needsAck: true,
      ackCounter: lastAckCounter,
      payload: pake3.toBuffer(),
    })

    const response = await sendUDP(msg, IP)
    expect(response).not.toBeNull()
    if (!response) return

    const parsed = parseResponse(response)
    expect(parsed).not.toBeNull()
    if (!parsed) return

    // Should be a StatusReport with success
    expect(parsed.opcode).toBe(OP_STATUS_REPORT)
    expect(parsed.protocolId).toBe(PROTO_SECURE_CHANNEL)
    lastAckCounter = parsed.msgCounter

    // Parse StatusReport (not TLV — raw binary: generalCode(2) + protocolId(4) + protocolCode(2))
    const sr = parsed.payload
    expect(sr.length).toBeGreaterThanOrEqual(8)
    const generalCode = sr.readUInt16LE(0)
    const protocolId = sr.readUInt32LE(2)
    const protocolCode = sr.readUInt16LE(6)

    expect(generalCode).toBe(0x0000)  // Success
    expect(protocolId).toBe(PROTO_SECURE_CHANNEL)
    expect(protocolCode).toBe(0x0000) // SessionEstablished

    // Derive session keys from Ke
    sessionKeys = deriveSessionKeys(Ke, null, 'SessionKeys')
    ;(globalThis as any).__sessionKeys = sessionKeys
    ;(globalThis as any).__responderSessionId = responderSessionId
    ;(globalThis as any).__lastAckCounter = lastAckCounter
    ;(globalThis as any).__paseCounter = counter
  })

  it('Session state should be PASE_ACTIVE after handshake', async () => {
    await new Promise(r => setTimeout(r, 200))
    const res = await fetch(`${BASE}/debug/state`)
    const state = await res.json()
    expect(state.sessionState).toBe('PASE_ACTIVE')
    expect(state.sessionSecure).toBe(true)
  })

  it('Device remains responsive after full PASE', async () => {
    const res = await fetch(`${BASE}/ping`)
    expect(res.status).toBe(200)
    expect(await res.text()).toBe('pong')
  })
})

// --- EC math helpers (inlined to avoid extra module) ---

function getECMath() {
  const P = BigInt('0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF')

  function mod(a: bigint, m: bigint): bigint {
    return ((a % m) + m) % m
  }

  function modInverse(a: bigint, m: bigint): bigint {
    let [old_r, r] = [a % m, m]
    let [old_s, s] = [1n, 0n]
    while (r !== 0n) {
      const q = old_r / r
      ;[old_r, r] = [r, old_r - q * r]
      ;[old_s, s] = [s, old_s - q * s]
    }
    return ((old_s % m) + m) % m
  }

  function pointFromBuf(buf: Buffer): { x: bigint; y: bigint } {
    return {
      x: BigInt('0x' + buf.subarray(1, 33).toString('hex')),
      y: BigInt('0x' + buf.subarray(33, 65).toString('hex')),
    }
  }

  function pointToBuf(x: bigint, y: bigint): Buffer {
    const buf = Buffer.alloc(65)
    buf[0] = 0x04
    Buffer.from(x.toString(16).padStart(64, '0'), 'hex').copy(buf, 1)
    Buffer.from(y.toString(16).padStart(64, '0'), 'hex').copy(buf, 33)
    return buf
  }

  function ecPointAdd(a: Buffer, b: Buffer): Buffer {
    const pa = pointFromBuf(a)
    const pb = pointFromBuf(b)

    if (pa.x === pb.x && pa.y === pb.y) {
      // Point doubling (P-256 has a = -3)
      const num = mod(3n * pa.x * pa.x + (P - 3n), P)
      const den = mod(2n * pa.y, P)
      const lambda = mod(num * modInverse(den, P), P)
      const xr = mod(lambda * lambda - 2n * pa.x, P)
      const yr = mod(lambda * (pa.x - xr) - pa.y, P)
      return pointToBuf(xr, yr)
    }

    const dx = mod(pb.x - pa.x, P)
    const dy = mod(pb.y - pa.y, P)
    const lambda = mod(dy * modInverse(dx, P), P)
    const xr = mod(lambda * lambda - pa.x - pb.x, P)
    const yr = mod(lambda * (pa.x - xr) - pa.y, P)
    return pointToBuf(xr, yr)
  }

  function ecPointNegate(p: Buffer): Buffer {
    const pt = pointFromBuf(p)
    return pointToBuf(pt.x, mod(-pt.y, P))
  }

  function ecScalarMul(point: Buffer, scalar: Buffer): Buffer {
    const k = BigInt('0x' + scalar.toString('hex'))
    let result: Buffer | null = null
    let current = Buffer.from(point)

    for (let i = 0; i < 256; i++) {
      if ((k >> BigInt(i)) & 1n) {
        result = result ? ecPointAdd(result, current) : Buffer.from(current)
      }
      current = ecPointAdd(current, current)
    }

    if (!result) throw new Error('Result is point at infinity')
    return result
  }

  return { ecPointAdd, ecPointNegate, ecScalarMul }
}
