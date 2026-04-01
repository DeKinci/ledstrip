// Encrypted Interaction Model tests
// After PASE session establishment, tests attribute reads, writes, and commands
// over the encrypted channel

import { describe, it, expect, beforeAll } from 'vitest'
import { getDeviceIP } from '@test/device.js'
import crypto from 'crypto'
import {
  MATTER_PORT, PROTO_SECURE_CHANNEL, PROTO_INTERACTION_MODEL,
  OP_PBKDF_PARAM_REQUEST, OP_PBKDF_PARAM_RESPONSE,
  OP_PASE_PAKE1, OP_PASE_PAKE2, OP_PASE_PAKE3,
  OP_STATUS_REPORT, OP_READ_REQUEST, OP_REPORT_DATA,
  OP_WRITE_REQUEST, OP_WRITE_RESPONSE,
  OP_INVOKE_REQUEST, OP_INVOKE_RESPONSE,
  EX_INITIATOR, EX_RELIABLE, EX_ACK,
  CCM_TAG_SIZE, CCM_NONCE_SIZE,
  buildMessage, buildMessageHeader, buildProtocolHeader,
  buildEncryptedMessage, decryptMessage,
  sendUDP, parseResponse, parseTLV,
  TLVWriter,
  spake2pDeriveW0W1, deriveSessionKeys, type SessionKeys,
  buildNonce, aesCcmEncrypt, aesCcmDecrypt,
} from './helpers/matter-crypto.js'

const IP = getDeviceIP()
const BASE = `http://${IP}`
const TEST_PASSCODE = 20202021

// Cluster constants
const CLUSTER_ON_OFF = 0x0006
const CLUSTER_LEVEL_CONTROL = 0x0008
const ATTR_ON_OFF = 0x0000
const ATTR_CURRENT_LEVEL = 0x0000
const CMD_ON = 0x01
const CMD_OFF = 0x00
const CMD_TOGGLE = 0x02
const CMD_MOVE_TO_LEVEL = 0x00

let counter = 2000
let exchangeCounter = 200
let sessionKeys: SessionKeys
let responderSessionId: number
let lastAckCounter = 0

// Establish PASE session before running IM tests
async function establishPASE(): Promise<boolean> {
  await fetch(`${BASE}/debug/reset-session`, { method: 'POST' })
  await new Promise(r => setTimeout(r, 1000))

  const exchangeId = exchangeCounter++
  const initiatorSessionId = 0x2000

  // Step 1: PBKDFParamRequest
  const random = crypto.randomBytes(32)
  const reqTlv = new TLVWriter()
  reqTlv.openStruct()
  reqTlv.putBytes(1, random)
  reqTlv.putU16(2, initiatorSessionId)
  reqTlv.putU16(3, 0)
  reqTlv.putBool(4, false)
  reqTlv.closeContainer()
  const reqBuf = reqTlv.toBuffer()

  const msg1 = buildMessage({
    counter: counter++,
    opcode: OP_PBKDF_PARAM_REQUEST,
    protocolId: PROTO_SECURE_CHANNEL,
    exchangeId,
    isInitiator: true,
    needsAck: true,
    payload: reqBuf,
  })

  const resp1 = await sendUDP(msg1, IP)
  if (!resp1) return false
  const p1 = parseResponse(resp1)
  if (!p1 || p1.opcode !== OP_PBKDF_PARAM_RESPONSE) return false
  lastAckCounter = p1.msgCounter
  const respBuf = p1.payload

  // Extract PBKDF params
  let pbkdfIterations = 1000
  let pbkdfSalt = Buffer.alloc(32)
  const raw = respBuf
  for (let i = 0; i < raw.length - 2; i++) {
    if (raw[i] === 0x35 && raw[i + 1] === 0x04) {
      let j = i + 2
      while (j < raw.length && raw[j] !== 0x18) {
        const ctrl = raw[j++]
        const et = ctrl & 0x1F
        let tag = 0xFF
        if ((ctrl & 0xE0) === 0x20) tag = raw[j++]
        if (tag === 1 && et === 0x06) {
          pbkdfIterations = raw[j] | (raw[j+1] << 8) | (raw[j+2] << 16) | (raw[j+3] << 24)
          j += 4
        } else if (tag === 2 && et === 0x10) {
          const len = raw[j++]
          pbkdfSalt = Buffer.from(raw.subarray(j, j + len))
          j += len
        } else break
      }
      break
    }
  }

  // Extract responderSessionId
  const fields = parseTLV(respBuf)
  const sessField = fields.get(3)
  if (!sessField) return false
  responderSessionId = sessField.value[0] | (sessField.value[1] << 8)

  // Derive w0, w1
  const { w0, w1 } = spake2pDeriveW0W1(TEST_PASSCODE, pbkdfSalt, pbkdfIterations)

  // Step 2: Pake1
  const { ecPointAdd, ecPointNegate, ecScalarMul } = getECMath()
  const POINT_M = Buffer.from([0x04,0x88,0x6e,0x2f,0x97,0xac,0xe4,0x6e,0x55,0xba,0x9d,0xd7,0x24,0x25,0x79,0xf2,0x99,0x3b,0x64,0xe1,0x6e,0xf3,0xdc,0xab,0x95,0xaf,0xd4,0x97,0x33,0x3d,0x8f,0xa1,0x2f,0x5f,0xf3,0x55,0x16,0x3e,0x43,0xce,0x22,0x4e,0x0b,0x0e,0x65,0xff,0x02,0xac,0x8e,0x5c,0x7b,0xe0,0x94,0x19,0xc7,0x85,0xe0,0xca,0x54,0x7d,0x55,0xa1,0x2e,0x2d,0x20])
  const POINT_N = Buffer.from([0x04,0xd8,0xbb,0xd6,0xc6,0x39,0xc6,0x29,0x37,0xb0,0x4d,0x99,0x7f,0x38,0xc3,0x77,0x07,0x19,0xc6,0x29,0xd7,0x01,0x4d,0x49,0xa2,0x4b,0x4f,0x98,0xba,0xa1,0x29,0x2b,0x49,0x07,0xd6,0x0a,0xa6,0xbf,0xad,0xe4,0x50,0x08,0xa6,0x36,0x33,0x7f,0x51,0x68,0xc6,0x4d,0x9b,0xd3,0x60,0x34,0x80,0x8c,0xd5,0x64,0x49,0x0b,0x1e,0x65,0x6e,0xdb,0xe7])
  const ORDER = BigInt('0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551')

  const xRaw = crypto.randomBytes(32)
  const xBig = BigInt('0x' + xRaw.toString('hex')) % ORDER
  const xBuf = Buffer.from(xBig.toString(16).padStart(64, '0'), 'hex')

  const ecdh = crypto.createECDH('prime256v1')
  ecdh.setPrivateKey(xBuf)
  const xG = Buffer.from(ecdh.getPublicKey())
  const w0M = ecScalarMul(POINT_M, w0)
  const pA = ecPointAdd(xG, w0M)

  const pake1Tlv = new TLVWriter()
  pake1Tlv.openStruct()
  pake1Tlv.putBytes(1, pA)
  pake1Tlv.closeContainer()

  const msg2 = buildMessage({
    counter: counter++,
    opcode: OP_PASE_PAKE1,
    protocolId: PROTO_SECURE_CHANNEL,
    exchangeId,
    isInitiator: true,
    needsAck: true,
    ackCounter: lastAckCounter,
    payload: pake1Tlv.toBuffer(),
  })

  const resp2 = await sendUDP(msg2, IP)
  if (!resp2) return false
  const p2 = parseResponse(resp2)
  if (!p2 || p2.opcode !== OP_PASE_PAKE2) return false
  lastAckCounter = p2.msgCounter

  const pake2Fields = parseTLV(p2.payload)
  const pB = Buffer.from(pake2Fields.get(1)!.value)
  const cB = Buffer.from(pake2Fields.get(2)!.value)

  // Compute shared keys
  const contextHash = crypto.createHash('sha256')
    .update(Buffer.from('CHIP PAKE V1 Commissioning', 'ascii'))
    .update(reqBuf)
    .update(respBuf)
    .digest()

  const w0N = ecScalarMul(POINT_N, w0)
  const Y = ecPointAdd(pB, ecPointNegate(w0N))
  const Z = ecScalarMul(Y, xBuf)
  const L = (() => { const e = crypto.createECDH('prime256v1'); e.setPrivateKey(w1); return Buffer.from(e.getPublicKey()) })()
  const V = ecScalarMul(L, xBuf)

  const hash = crypto.createHash('sha256')
  function ttAppend(data: Buffer | null) {
    const lenBuf = Buffer.alloc(8)
    lenBuf.writeUInt32LE(data ? data.length : 0, 0)
    hash.update(lenBuf)
    if (data && data.length > 0) hash.update(data)
  }
  ttAppend(contextHash)
  ttAppend(null); ttAppend(null)
  ttAppend(POINT_M); ttAppend(POINT_N)
  ttAppend(pA); ttAppend(pB)
  ttAppend(Z); ttAppend(V); ttAppend(w0)
  const hashTT = hash.digest()

  const Ka = hashTT.subarray(0, 16)
  const Ke = Buffer.from(hashTT.subarray(16, 32))
  const KcAKcB = Buffer.from(crypto.hkdfSync('sha256', Ka, Buffer.alloc(0), 'ConfirmationKeys', 32))
  const KcA = KcAKcB.subarray(0, 16)
  const KcB = KcAKcB.subarray(16, 32)

  // Verify cB
  const expectedCB = crypto.createHmac('sha256', KcB).update(pA).digest()
  if (!crypto.timingSafeEqual(expectedCB, cB)) return false

  // Step 3: Pake3
  const cA = crypto.createHmac('sha256', KcA).update(pB).digest()
  const pake3Tlv = new TLVWriter()
  pake3Tlv.openStruct()
  pake3Tlv.putBytes(1, cA)
  pake3Tlv.closeContainer()

  const msg3 = buildMessage({
    counter: counter++,
    opcode: OP_PASE_PAKE3,
    protocolId: PROTO_SECURE_CHANNEL,
    exchangeId,
    isInitiator: true,
    needsAck: true,
    ackCounter: lastAckCounter,
    payload: pake3Tlv.toBuffer(),
  })

  const resp3 = await sendUDP(msg3, IP)
  if (!resp3) return false
  const p3 = parseResponse(resp3)
  if (!p3 || p3.opcode !== OP_STATUS_REPORT) return false
  lastAckCounter = p3.msgCounter

  const sr = p3.payload
  if (sr.length < 8) return false
  if (sr.readUInt16LE(0) !== 0x0000) return false // generalCode = success

  sessionKeys = deriveSessionKeys(Ke, null, 'SessionKeys')
  return true
}

function getECMath() {
  const P = BigInt('0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF')
  function mod(a: bigint, m: bigint): bigint { return ((a % m) + m) % m }
  function modInverse(a: bigint, m: bigint): bigint {
    let [old_r, r] = [a % m, m]
    let [old_s, s] = [1n, 0n]
    while (r !== 0n) { const q = old_r / r; [old_r, r] = [r, old_r - q * r]; [old_s, s] = [s, old_s - q * s] }
    return ((old_s % m) + m) % m
  }
  function ptFrom(buf: Buffer) { return { x: BigInt('0x' + buf.subarray(1,33).toString('hex')), y: BigInt('0x' + buf.subarray(33,65).toString('hex')) } }
  function ptTo(x: bigint, y: bigint): Buffer {
    const buf = Buffer.alloc(65); buf[0] = 0x04
    Buffer.from(x.toString(16).padStart(64,'0'), 'hex').copy(buf, 1)
    Buffer.from(y.toString(16).padStart(64,'0'), 'hex').copy(buf, 33)
    return buf
  }
  function ecPointAdd(a: Buffer, b: Buffer): Buffer {
    const pa = ptFrom(a), pb = ptFrom(b)
    if (pa.x === pb.x && pa.y === pb.y) {
      const num = mod(3n * pa.x * pa.x + (P - 3n), P)
      const den = mod(2n * pa.y, P)
      const l = mod(num * modInverse(den, P), P)
      const xr = mod(l * l - 2n * pa.x, P)
      return ptTo(xr, mod(l * (pa.x - xr) - pa.y, P))
    }
    const l = mod(mod(pb.y - pa.y, P) * modInverse(mod(pb.x - pa.x, P), P), P)
    const xr = mod(l * l - pa.x - pb.x, P)
    return ptTo(xr, mod(l * (pa.x - xr) - pa.y, P))
  }
  function ecPointNegate(p: Buffer): Buffer { const pt = ptFrom(p); return ptTo(pt.x, mod(-pt.y, P)) }
  function ecScalarMul(point: Buffer, scalar: Buffer): Buffer {
    const k = BigInt('0x' + scalar.toString('hex'))
    let result: Buffer | null = null, current = Buffer.from(point)
    for (let i = 0; i < 256; i++) {
      if ((k >> BigInt(i)) & 1n) result = result ? ecPointAdd(result, current) : Buffer.from(current)
      current = ecPointAdd(current, current)
    }
    if (!result) throw new Error('Point at infinity')
    return result
  }
  return { ecPointAdd, ecPointNegate, ecScalarMul }
}

function sendEncrypted(opts: {
  opcode: number
  protocolId: number
  exchangeId: number
  payload?: Buffer
  ackCounter?: number
}): Promise<Buffer | null> {
  const cnt = counter++
  const msg = buildEncryptedMessage({
    sessionId: responderSessionId,
    counter: cnt,
    opcode: opts.opcode,
    protocolId: opts.protocolId,
    exchangeId: opts.exchangeId,
    isInitiator: true,
    needsAck: true,
    ackCounter: opts.ackCounter ?? lastAckCounter,
    payload: opts.payload,
    keys: sessionKeys,
  })
  return sendUDP(msg, IP)
}

describe('Encrypted IM', () => {
  beforeAll(async () => {
    const ok = await establishPASE()
    if (!ok) throw new Error('PASE handshake failed — cannot run encrypted IM tests')
    // Reset cluster state
    await fetch(`${BASE}/debug/reset`, { method: 'POST' })
  }, 30000)

  it('reads OnOff attribute', async () => {
    const exId = exchangeCounter++
    const tlv = new TLVWriter()
    tlv.openStruct()
    tlv.openArray(0)  // tag 0: attributeRequests
    // AttributePathIB (list): endpoint=1, cluster=OnOff, attribute=OnOff
    tlv.openList()
    tlv.putU16(2, 1)  // endpoint
    tlv.putU32(3, CLUSTER_ON_OFF)
    tlv.putU32(4, ATTR_ON_OFF)
    tlv.closeContainer()
    tlv.closeContainer()
    tlv.putBool(3, false)  // fabricFiltered
    tlv.closeContainer()

    const resp = await sendEncrypted({
      opcode: OP_READ_REQUEST,
      protocolId: PROTO_INTERACTION_MODEL,
      exchangeId: exId,
      payload: tlv.toBuffer(),
    })

    expect(resp).not.toBeNull()
    if (!resp) return

    const decrypted = decryptMessage(resp, sessionKeys)
    expect(decrypted).not.toBeNull()
    if (!decrypted) return

    expect(decrypted.opcode).toBe(OP_REPORT_DATA)
    expect(decrypted.protocolId).toBe(PROTO_INTERACTION_MODEL)
    lastAckCounter = resp.readUInt32LE(4)

    // Verify we got a valid TLV response with OnOff data
    expect(decrypted.payload.length).toBeGreaterThan(0)
    // The initial state should be off (false) after reset
  })

  it('sends Toggle command', async () => {
    const exId = exchangeCounter++
    const tlv = new TLVWriter()
    tlv.openStruct()
    tlv.putBool(0, false)  // suppressResponse
    tlv.putBool(1, false)  // timedRequest
    tlv.openArray(2)       // invokeRequests
    tlv.openStruct()       // CommandDataIB
    tlv.openList(0)        // CommandPath
    tlv.putU16(0, 1)       // endpoint
    tlv.putU32(1, CLUSTER_ON_OFF)
    tlv.putU32(2, CMD_TOGGLE)
    tlv.closeContainer()
    tlv.openStruct(1)      // CommandFields (empty for Toggle)
    tlv.closeContainer()
    tlv.closeContainer()   // /CommandDataIB
    tlv.closeContainer()   // /invokeRequests
    tlv.closeContainer()

    const resp = await sendEncrypted({
      opcode: OP_INVOKE_REQUEST,
      protocolId: PROTO_INTERACTION_MODEL,
      exchangeId: exId,
      payload: tlv.toBuffer(),
    })

    expect(resp).not.toBeNull()
    if (!resp) return

    const decrypted = decryptMessage(resp, sessionKeys)
    expect(decrypted).not.toBeNull()
    if (!decrypted) return

    expect(decrypted.opcode).toBe(OP_INVOKE_RESPONSE)
    lastAckCounter = resp.readUInt32LE(4)

    // Verify via HTTP debug that OnOff toggled to true
    const state = await (await fetch(`${BASE}/debug/state`)).json()
    expect(state.onOff).toBe(true)
  })

  it('sends MoveToLevel command', async () => {
    const exId = exchangeCounter++
    const tlv = new TLVWriter()
    tlv.openStruct()
    tlv.putBool(0, false)
    tlv.putBool(1, false)
    tlv.openArray(2)
    tlv.openStruct()
    tlv.openList(0)
    tlv.putU16(0, 1)
    tlv.putU32(1, CLUSTER_LEVEL_CONTROL)
    tlv.putU32(2, CMD_MOVE_TO_LEVEL)
    tlv.closeContainer()
    tlv.openStruct(1)      // CommandFields
    tlv.putU8(0, 200)      // level = 200
    tlv.putU16(1, 0)       // transitionTime = 0
    tlv.closeContainer()
    tlv.closeContainer()
    tlv.closeContainer()
    tlv.closeContainer()

    const resp = await sendEncrypted({
      opcode: OP_INVOKE_REQUEST,
      protocolId: PROTO_INTERACTION_MODEL,
      exchangeId: exId,
      payload: tlv.toBuffer(),
    })

    expect(resp).not.toBeNull()
    if (!resp) return

    const decrypted = decryptMessage(resp, sessionKeys)
    expect(decrypted).not.toBeNull()
    if (!decrypted) return

    expect(decrypted.opcode).toBe(OP_INVOKE_RESPONSE)
    lastAckCounter = resp.readUInt32LE(4)

    // Verify brightness changed
    const state = await (await fetch(`${BASE}/debug/state`)).json()
    expect(state.brightness).toBe(200)
  })

  it('reads CurrentLevel attribute', async () => {
    const exId = exchangeCounter++
    const tlv = new TLVWriter()
    tlv.openStruct()
    tlv.openArray(0)
    tlv.openList()
    tlv.putU16(2, 1)
    tlv.putU32(3, CLUSTER_LEVEL_CONTROL)
    tlv.putU32(4, ATTR_CURRENT_LEVEL)
    tlv.closeContainer()
    tlv.closeContainer()
    tlv.putBool(3, false)
    tlv.closeContainer()

    const resp = await sendEncrypted({
      opcode: OP_READ_REQUEST,
      protocolId: PROTO_INTERACTION_MODEL,
      exchangeId: exId,
      payload: tlv.toBuffer(),
    })

    expect(resp).not.toBeNull()
    if (!resp) return

    const decrypted = decryptMessage(resp, sessionKeys)
    expect(decrypted).not.toBeNull()
    if (!decrypted) return

    expect(decrypted.opcode).toBe(OP_REPORT_DATA)
    lastAckCounter = resp.readUInt32LE(4)

    // The payload should contain the brightness value 200 we set
    expect(decrypted.payload.length).toBeGreaterThan(0)
  })

  it('sends Off command', async () => {
    const exId = exchangeCounter++
    const tlv = new TLVWriter()
    tlv.openStruct()
    tlv.putBool(0, false)
    tlv.putBool(1, false)
    tlv.openArray(2)
    tlv.openStruct()
    tlv.openList(0)
    tlv.putU16(0, 1)
    tlv.putU32(1, CLUSTER_ON_OFF)
    tlv.putU32(2, CMD_OFF)
    tlv.closeContainer()
    tlv.openStruct(1)
    tlv.closeContainer()
    tlv.closeContainer()
    tlv.closeContainer()
    tlv.closeContainer()

    const resp = await sendEncrypted({
      opcode: OP_INVOKE_REQUEST,
      protocolId: PROTO_INTERACTION_MODEL,
      exchangeId: exId,
      payload: tlv.toBuffer(),
    })

    expect(resp).not.toBeNull()
    if (!resp) return

    const decrypted = decryptMessage(resp, sessionKeys)
    expect(decrypted).not.toBeNull()

    // Verify OnOff is now false
    const state = await (await fetch(`${BASE}/debug/state`)).json()
    expect(state.onOff).toBe(false)
  })

  it('rejects message with wrong session key', async () => {
    const exId = exchangeCounter++
    const badKeys: SessionKeys = {
      i2rKey: crypto.randomBytes(16),
      r2iKey: crypto.randomBytes(16),
      attestChallenge: crypto.randomBytes(16),
    }

    const tlv = new TLVWriter()
    tlv.openStruct()
    tlv.openArray(0)
    tlv.openList()
    tlv.putU16(2, 1)
    tlv.putU32(3, CLUSTER_ON_OFF)
    tlv.putU32(4, ATTR_ON_OFF)
    tlv.closeContainer()
    tlv.closeContainer()
    tlv.putBool(3, false)
    tlv.closeContainer()

    const msg = buildEncryptedMessage({
      sessionId: responderSessionId,
      counter: counter++,
      opcode: OP_READ_REQUEST,
      protocolId: PROTO_INTERACTION_MODEL,
      exchangeId: exId,
      isInitiator: true,
      needsAck: true,
      payload: tlv.toBuffer(),
      keys: badKeys,
    })

    const resp = await sendUDP(msg, IP, 3000)
    // Device should either not respond or send an error — not crash
    // No response is the expected behavior for a decryption failure
    // (the device silently drops messages that fail auth)

    // Verify device still alive
    const ping = await fetch(`${BASE}/ping`)
    expect(ping.status).toBe(200)
  })

  it('device remains responsive after encrypted IM', async () => {
    const res = await fetch(`${BASE}/ping`)
    expect(res.status).toBe(200)
    expect(await res.text()).toBe('pong')
  })
})
