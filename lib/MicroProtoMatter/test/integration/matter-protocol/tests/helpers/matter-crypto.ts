// Matter protocol crypto helpers for integration tests
// Implements client-side SPAKE2+, session key derivation, and AES-CCM

import crypto from 'crypto'

// --- Constants ---
export const MATTER_PORT = 5540
export const PROTO_SECURE_CHANNEL = 0x0000
export const PROTO_INTERACTION_MODEL = 0x0001

export const OP_PBKDF_PARAM_REQUEST = 0x20
export const OP_PBKDF_PARAM_RESPONSE = 0x21
export const OP_PASE_PAKE1 = 0x22
export const OP_PASE_PAKE2 = 0x23
export const OP_PASE_PAKE3 = 0x24
export const OP_STATUS_REPORT = 0x40

export const OP_READ_REQUEST = 0x02
export const OP_REPORT_DATA = 0x05
export const OP_WRITE_REQUEST = 0x06
export const OP_WRITE_RESPONSE = 0x07
export const OP_INVOKE_REQUEST = 0x08
export const OP_INVOKE_RESPONSE = 0x09
export const OP_SUBSCRIBE_REQUEST = 0x03
export const OP_SUBSCRIBE_RESPONSE = 0x04
export const OP_STATUS_RESPONSE = 0x01

export const EX_INITIATOR = 0x01
export const EX_ACK = 0x02
export const EX_RELIABLE = 0x04

export const CCM_TAG_SIZE = 16
export const CCM_NONCE_SIZE = 13

// SPAKE2+ M and N points (uncompressed P-256, from Matter spec / RFC 9383)
const POINT_M = Buffer.from([
  0x04,
  0x88, 0x6e, 0x2f, 0x97, 0xac, 0xe4, 0x6e, 0x55,
  0xba, 0x9d, 0xd7, 0x24, 0x25, 0x79, 0xf2, 0x99,
  0x3b, 0x64, 0xe1, 0x6e, 0xf3, 0xdc, 0xab, 0x95,
  0xaf, 0xd4, 0x97, 0x33, 0x3d, 0x8f, 0xa1, 0x2f,
  0x5f, 0xf3, 0x55, 0x16, 0x3e, 0x43, 0xce, 0x22,
  0x4e, 0x0b, 0x0e, 0x65, 0xff, 0x02, 0xac, 0x8e,
  0x5c, 0x7b, 0xe0, 0x94, 0x19, 0xc7, 0x85, 0xe0,
  0xca, 0x54, 0x7d, 0x55, 0xa1, 0x2e, 0x2d, 0x20,
])
const POINT_N = Buffer.from([
  0x04,
  0xd8, 0xbb, 0xd6, 0xc6, 0x39, 0xc6, 0x29, 0x37,
  0xb0, 0x4d, 0x99, 0x7f, 0x38, 0xc3, 0x77, 0x07,
  0x19, 0xc6, 0x29, 0xd7, 0x01, 0x4d, 0x49, 0xa2,
  0x4b, 0x4f, 0x98, 0xba, 0xa1, 0x29, 0x2b, 0x49,
  0x07, 0xd6, 0x0a, 0xa6, 0xbf, 0xad, 0xe4, 0x50,
  0x08, 0xa6, 0x36, 0x33, 0x7f, 0x51, 0x68, 0xc6,
  0x4d, 0x9b, 0xd3, 0x60, 0x34, 0x80, 0x8c, 0xd5,
  0x64, 0x49, 0x0b, 0x1e, 0x65, 0x6e, 0xdb, 0xe7,
])

// P-256 curve order
const P256_ORDER = BigInt('0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551')

// --- EC point arithmetic helpers using Node.js crypto ---

function bigintToBuffer(n: bigint, len: number): Buffer {
  const hex = n.toString(16).padStart(len * 2, '0')
  return Buffer.from(hex, 'hex')
}

function bufferToBigint(buf: Buffer): bigint {
  return BigInt('0x' + buf.toString('hex'))
}

// Multiply an EC point by a scalar using ECDH trick:
// Create ephemeral key with scalar as private key, compute ECDH with the point
// This only gives us the x-coordinate, so we use the crypto.createECDH approach
function ecMultiply(point: Buffer, scalar: Buffer): Buffer {
  // Use OpenSSL's EC_POINT_mul via a workaround:
  // We create an ECDH object, set the private key to our scalar,
  // and compute the shared secret with the point
  const ecdh = crypto.createECDH('prime256v1')
  ecdh.setPrivateKey(scalar)
  // computeSecret returns just the x-coordinate, but we need the full point
  // Instead, generateKeys with our scalar to get the corresponding public key
  // which is scalar * G
  return Buffer.from(ecdh.getPublicKey())
}

// scalar * G (generator point multiplication)
function ecScalarMulG(scalar: Buffer): Buffer {
  const ecdh = crypto.createECDH('prime256v1')
  ecdh.setPrivateKey(scalar)
  return Buffer.from(ecdh.getPublicKey())
}

// Point addition: A + B using the EC library
// Node.js crypto doesn't expose point addition directly, so we use
// the OpenSSL binding through the createPublicKey/diffieHellman approach
// Actually, we need a different strategy. Let's use the ECDH computeSecret
// to get x*P, then implement addition via the curve equation.
//
// For SPAKE2+, we need: pA = x*G + w0*M
// We can compute x*G and w0*M separately, then add them.
// Unfortunately, Node.js crypto doesn't expose EC point addition.
// We'll use the `@noble/curves` approach or implement it ourselves.
//
// Actually, the simplest approach: use createECDH to compute scalar*Point
// for both terms, then add the uncompressed points using affine coordinates.

const P = BigInt('0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF')

function modInverse(a: bigint, m: bigint): bigint {
  // Extended Euclidean algorithm
  let [old_r, r] = [a % m, m]
  let [old_s, s] = [1n, 0n]
  while (r !== 0n) {
    const q = old_r / r
    ;[old_r, r] = [r, old_r - q * r]
    ;[old_s, s] = [s, old_s - q * s]
  }
  return ((old_s % m) + m) % m
}

function mod(a: bigint, m: bigint): bigint {
  return ((a % m) + m) % m
}

function pointFromUncompressed(buf: Buffer): { x: bigint; y: bigint } {
  if (buf[0] !== 0x04 || buf.length !== 65) throw new Error('Expected uncompressed P-256 point')
  return {
    x: bufferToBigint(buf.subarray(1, 33)),
    y: bufferToBigint(buf.subarray(33, 65)),
  }
}

function pointToUncompressed(x: bigint, y: bigint): Buffer {
  const buf = Buffer.alloc(65)
  buf[0] = 0x04
  bigintToBuffer(x, 32).copy(buf, 1)
  bigintToBuffer(y, 32).copy(buf, 33)
  return buf
}

function ecPointAdd(a: Buffer, b: Buffer): Buffer {
  const pa = pointFromUncompressed(a)
  const pb = pointFromUncompressed(b)

  if (pa.x === pb.x && pa.y === pb.y) {
    // Point doubling
    const num = mod(3n * pa.x * pa.x, P) // a = 0 for P-256 is wrong, a = -3
    const num2 = mod(3n * pa.x * pa.x + (P - 3n), P) // P-256 has a = -3
    const den = mod(2n * pa.y, P)
    const lambda = mod(num2 * modInverse(den, P), P)
    const xr = mod(lambda * lambda - pa.x - pb.x, P)
    const yr = mod(lambda * (pa.x - xr) - pa.y, P)
    return pointToUncompressed(xr, yr)
  }

  // Point addition
  const lambda = mod((pb.y - pa.y) * modInverse(mod(pb.x - pa.x, P), P), P)
  const xr = mod(lambda * lambda - pa.x - pb.x, P)
  const yr = mod(lambda * (pa.x - xr) - pa.y, P)
  return pointToUncompressed(xr, yr)
}

function ecPointNegate(p: Buffer): Buffer {
  const pt = pointFromUncompressed(p)
  return pointToUncompressed(pt.x, mod(-pt.y, P))
}

// Scalar multiplication using double-and-add (on uncompressed points)
function ecScalarMul(point: Buffer, scalar: Buffer): Buffer {
  const k = bufferToBigint(scalar)
  if (k === 0n) throw new Error('Zero scalar')

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

// --- SPAKE2+ (prover/commissioner side) ---

export interface Spake2pResult {
  pA: Buffer        // Our public share (sent in Pake1)
  Ke: Buffer        // Encryption key (16 bytes)
  KcA: Buffer       // Our confirmation key
  KcB: Buffer       // Peer confirmation key
  cA: Buffer        // Our confirmation value (sent in Pake3)
}

export function spake2pDeriveW0W1(
  passcode: number,
  salt: Buffer,
  iterations: number,
): { w0: Buffer; w1: Buffer } {
  // Passcode as 4-byte LE
  const pw = Buffer.alloc(4)
  pw.writeUInt32LE(passcode)

  // PBKDF2-SHA256 → 80 bytes
  const ws = crypto.pbkdf2Sync(pw, salt, iterations, 80, 'sha256')

  // Reduce mod curve order
  const w0s = bufferToBigint(ws.subarray(0, 40))
  const w1s = bufferToBigint(ws.subarray(40, 80))
  const w0 = bigintToBuffer(w0s % P256_ORDER, 32)
  const w1 = bigintToBuffer(w1s % P256_ORDER, 32)

  return { w0, w1 }
}

export function spake2pComputeProver(
  w0: Buffer,
  w1: Buffer,
  pB: Buffer,           // Responder's share (from Pake2)
  contextHash: Buffer,  // SHA-256 of "CHIP PAKE V1 Commissioning" || req || resp
): Spake2pResult {
  // Generate random scalar x
  const x = crypto.randomBytes(32)
  // Ensure x < order
  const xBig = bufferToBigint(x) % P256_ORDER
  const xBuf = bigintToBuffer(xBig, 32)

  // X = x * G
  const xG = ecScalarMulG(xBuf)

  // pA = X + w0 * M
  const w0M = ecScalarMul(POINT_M, w0)
  const pA = ecPointAdd(xG, w0M)

  // Y = pB - w0 * N
  const w0N = ecScalarMul(POINT_N, w0)
  const Y = ecPointAdd(pB, ecPointNegate(w0N))

  // Z = x * Y
  const Z = ecScalarMul(Y, xBuf)

  // V = x * L where L = w1 * G
  const L = ecScalarMulG(w1)
  const V = ecScalarMul(L, xBuf)

  // Build transcript hash TT
  const hash = crypto.createHash('sha256')

  // Helper: append length (8-byte LE) + data
  function ttAppend(data: Buffer | null) {
    const lenBuf = Buffer.alloc(8)
    const len = data ? data.length : 0
    lenBuf.writeUInt32LE(len, 0)
    hash.update(lenBuf)
    if (data && len > 0) hash.update(data)
  }

  ttAppend(contextHash)  // Context
  ttAppend(null)          // A (empty for Matter PASE)
  ttAppend(null)          // B (empty for Matter PASE)
  ttAppend(POINT_M)       // M
  ttAppend(POINT_N)       // N
  ttAppend(pA)            // X = pA (prover's share)
  ttAppend(pB)            // Y = pB (verifier's share)
  ttAppend(Z)             // Z
  ttAppend(V)             // V
  ttAppend(w0)            // w0

  const hashTT = hash.digest()

  // Ka || Ke = hashTT split in half
  const Ka = hashTT.subarray(0, 16)
  const Ke = Buffer.from(hashTT.subarray(16, 32))

  // KcA || KcB = HKDF(salt=nil, IKM=Ka, info="ConfirmationKeys", L=32)
  const KcAKcB = crypto.hkdfSync('sha256', Ka, Buffer.alloc(0), 'ConfirmationKeys', 32)
  const KcA = Buffer.from(KcAKcB.slice(0, 16))
  const KcB = Buffer.from(KcAKcB.slice(16, 32))

  // cA = HMAC(KcA, pB) — prover MACs the verifier's share
  const cA = Buffer.from(crypto.createHmac('sha256', KcA).update(pB).digest())

  return { pA, Ke, KcA, KcB, cA }
}

export function spake2pVerifyCb(KcB: Buffer, pA: Buffer, cB: Buffer): boolean {
  // Expected: cB = HMAC(KcB, pA) — verifier MACs the prover's share
  const expected = crypto.createHmac('sha256', KcB).update(pA).digest()
  return crypto.timingSafeEqual(expected, cB)
}

// --- Session key derivation ---

export interface SessionKeys {
  i2rKey: Buffer   // Initiator → Responder (our encrypt key)
  r2iKey: Buffer   // Responder → Initiator (our decrypt key)
  attestChallenge: Buffer
}

export function deriveSessionKeys(
  secret: Buffer,
  salt: Buffer | null,
  info: string,
): SessionKeys {
  const keyMaterial = Buffer.from(
    crypto.hkdfSync('sha256', secret, salt || Buffer.alloc(0), info, 48)
  )
  return {
    i2rKey: keyMaterial.subarray(0, 16),
    r2iKey: keyMaterial.subarray(16, 32),
    attestChallenge: keyMaterial.subarray(32, 48),
  }
}

// --- AES-128-CCM ---

export function aesCcmEncrypt(
  key: Buffer, nonce: Buffer, aad: Buffer,
  plaintext: Buffer,
): { ciphertext: Buffer; tag: Buffer } {
  const cipher = crypto.createCipheriv('aes-128-ccm', key, nonce, {
    authTagLength: CCM_TAG_SIZE,
  })
  cipher.setAAD(aad, { plaintextLength: plaintext.length })
  const ciphertext = Buffer.concat([cipher.update(plaintext), cipher.final()])
  const tag = cipher.getAuthTag()
  return { ciphertext, tag }
}

export function aesCcmDecrypt(
  key: Buffer, nonce: Buffer, aad: Buffer,
  ciphertext: Buffer, tag: Buffer,
): Buffer | null {
  try {
    const decipher = crypto.createDecipheriv('aes-128-ccm', key, nonce, {
      authTagLength: CCM_TAG_SIZE,
    })
    decipher.setAuthTag(tag)
    decipher.setAAD(aad, { plaintextLength: ciphertext.length })
    const plain = Buffer.concat([decipher.update(ciphertext), decipher.final()])
    return plain
  } catch {
    return null
  }
}

// --- Matter message building/parsing ---

export function buildNonce(secFlags: number, msgCounter: number, nodeId: bigint): Buffer {
  const nonce = Buffer.alloc(CCM_NONCE_SIZE)
  nonce[0] = secFlags
  nonce.writeUInt32LE(msgCounter, 1)
  nonce.writeBigUInt64LE(nodeId, 5)
  return nonce
}

export function buildMessageHeader(opts: {
  sessionId: number
  counter: number
  hasSrc?: boolean
  sourceNodeId?: bigint
}): Buffer {
  const hasSrc = opts.hasSrc && opts.sourceNodeId !== undefined
  const buf = Buffer.alloc(8 + (hasSrc ? 8 : 0))
  let flags = 0x00
  if (hasSrc) flags |= 0x04 // S-bit
  buf[0] = flags
  buf[1] = 0x00
  buf.writeUInt16LE(opts.sessionId, 2)
  buf.writeUInt32LE(opts.counter, 4)
  if (hasSrc) buf.writeBigUInt64LE(opts.sourceNodeId!, 8)
  return buf
}

export function buildProtocolHeader(opts: {
  opcode: number
  protocolId: number
  exchangeId: number
  isInitiator?: boolean
  needsAck?: boolean
  ackCounter?: number
}): Buffer {
  const hasAck = opts.ackCounter !== undefined
  const buf = Buffer.alloc(6 + (hasAck ? 4 : 0))
  let flags = 0
  if (opts.isInitiator) flags |= EX_INITIATOR
  if (opts.needsAck) flags |= EX_RELIABLE
  if (hasAck) flags |= EX_ACK
  buf[0] = flags
  buf[1] = opts.opcode
  buf.writeUInt16LE(opts.exchangeId, 2)
  buf.writeUInt16LE(opts.protocolId, 4)
  if (hasAck) buf.writeUInt32LE(opts.ackCounter!, 6)
  return buf
}

export function buildMessage(opts: {
  sessionId?: number
  counter: number
  opcode: number
  protocolId: number
  exchangeId: number
  isInitiator?: boolean
  needsAck?: boolean
  ackCounter?: number
  payload?: Buffer
}): Buffer {
  const msgHdr = buildMessageHeader({
    sessionId: opts.sessionId ?? 0,
    counter: opts.counter,
  })
  const protoHdr = buildProtocolHeader({
    opcode: opts.opcode,
    protocolId: opts.protocolId,
    exchangeId: opts.exchangeId,
    isInitiator: opts.isInitiator,
    needsAck: opts.needsAck,
    ackCounter: opts.ackCounter,
  })
  return Buffer.concat([msgHdr, protoHdr, opts.payload || Buffer.alloc(0)])
}

export function buildEncryptedMessage(opts: {
  sessionId: number
  counter: number
  opcode: number
  protocolId: number
  exchangeId: number
  isInitiator?: boolean
  needsAck?: boolean
  ackCounter?: number
  payload?: Buffer
  keys: SessionKeys
  sourceNodeId?: bigint
}): Buffer {
  const msgHdr = buildMessageHeader({
    sessionId: opts.sessionId,
    counter: opts.counter,
  })
  const protoHdr = buildProtocolHeader({
    opcode: opts.opcode,
    protocolId: opts.protocolId,
    exchangeId: opts.exchangeId,
    isInitiator: opts.isInitiator,
    needsAck: opts.needsAck,
    ackCounter: opts.ackCounter,
  })
  const plaintext = Buffer.concat([protoHdr, opts.payload || Buffer.alloc(0)])
  const nonce = buildNonce(0x00, opts.counter, opts.sourceNodeId ?? 0n)
  const { ciphertext, tag } = aesCcmEncrypt(opts.keys.i2rKey, nonce, msgHdr, plaintext)
  return Buffer.concat([msgHdr, ciphertext, tag])
}

export function decryptMessage(
  data: Buffer,
  keys: SessionKeys,
): { msgCounter: number; opcode: number; exchangeId: number; protocolId: number; payload: Buffer } | null {
  if (data.length < 8 + CCM_TAG_SIZE) return null

  // Parse message header (8 bytes for basic, no src/dest)
  const flags = data[0]
  const sessionId = data.readUInt16LE(2)
  const msgCounter = data.readUInt32LE(4)
  let hdrLen = 8
  if (flags & 0x04) hdrLen += 8 // source node ID
  if ((flags & 0x03) === 0x01) hdrLen += 8 // dest node ID

  const msgHdr = data.subarray(0, hdrLen)
  const cipherAndTag = data.subarray(hdrLen)
  if (cipherAndTag.length < CCM_TAG_SIZE) return null

  const ciphertext = cipherAndTag.subarray(0, cipherAndTag.length - CCM_TAG_SIZE)
  const tag = cipherAndTag.subarray(cipherAndTag.length - CCM_TAG_SIZE)

  // For device→initiator, device is responder, so use r2iKey for decryption
  // Nonce uses device's node ID as source (0 for PASE)
  const nonce = buildNonce(data[1], msgCounter, 0n)
  const plain = aesCcmDecrypt(keys.r2iKey, nonce, msgHdr, ciphertext, tag)
  if (!plain) return null

  // Parse protocol header from plaintext
  if (plain.length < 6) return null
  const opcode = plain[1]
  const exchangeId = plain.readUInt16LE(2)
  const protocolId = plain.readUInt16LE(4)
  const hasAck = (plain[0] & EX_ACK) !== 0
  const payloadOffset = 6 + (hasAck ? 4 : 0)

  return {
    msgCounter,
    opcode,
    exchangeId,
    protocolId,
    payload: plain.subarray(payloadOffset),
  }
}

// --- Simple TLV builder ---

export class TLVWriter {
  private parts: number[] = []

  openStruct(tag?: number) { this.writeTag(tag, 0x15) }
  openArray(tag?: number)  { this.writeTag(tag, 0x16) }
  openList(tag?: number)   { this.writeTag(tag, 0x17) }
  closeContainer() { this.parts.push(0x18) }

  putBool(tag: number, v: boolean) { this.writeTag(tag, v ? 0x09 : 0x08) }
  putU8(tag: number, v: number) { this.writeTag(tag, 0x04); this.parts.push(v & 0xFF) }
  putU16(tag: number, v: number) { this.writeTag(tag, 0x05); this.pushLE16(v) }
  putU32(tag: number, v: number) { this.writeTag(tag, 0x06); this.pushLE32(v) }
  putBytes(tag: number, data: Buffer) {
    if (data.length <= 0xFF) {
      this.writeTag(tag, 0x10)
      this.parts.push(data.length)
    } else {
      this.writeTag(tag, 0x11)
      this.pushLE16(data.length)
    }
    for (const b of data) this.parts.push(b)
  }
  putString(tag: number, s: string) {
    const buf = Buffer.from(s, 'utf-8')
    if (buf.length <= 0xFF) {
      this.writeTag(tag, 0x0C)
      this.parts.push(buf.length)
    } else {
      this.writeTag(tag, 0x0D)
      this.pushLE16(buf.length)
    }
    for (const b of buf) this.parts.push(b)
  }

  toBuffer(): Buffer { return Buffer.from(this.parts) }

  private writeTag(tag: number | undefined, type: number) {
    if (tag === undefined) {
      this.parts.push(type) // anonymous
    } else {
      this.parts.push(0x20 | type) // context tag
      this.parts.push(tag)
    }
  }

  private pushLE16(v: number) {
    this.parts.push(v & 0xFF, (v >> 8) & 0xFF)
  }
  private pushLE32(v: number) {
    this.parts.push(v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF)
  }
}

// --- Simple TLV reader ---

export function parseTLV(data: Buffer): Map<number, { type: number; value: Buffer }> {
  const result = new Map<number, { type: number; value: Buffer }>()
  let pos = 0
  if (pos >= data.length) return result
  if (data[pos] === 0x15) pos++ // Skip struct open

  while (pos < data.length && data[pos] !== 0x18) {
    const ctrl = data[pos++]
    const tagForm = ctrl & 0xE0
    const elemType = ctrl & 0x1F
    let tag = 0xFF

    if (tagForm === 0x20) tag = data[pos++]

    let vStart = pos
    let vLen = 0

    switch (elemType) {
      case 0x04: case 0x00: vLen = 1; pos += 1; break
      case 0x05: case 0x01: vLen = 2; pos += 2; break
      case 0x06: case 0x02: case 0x0A: vLen = 4; pos += 4; break
      case 0x07: case 0x03: case 0x0B: vLen = 8; pos += 8; break
      case 0x08: case 0x09: case 0x14: break // bool/null: no data
      case 0x10: case 0x0C: { // bytes1 / utf8_1
        vLen = data[pos++]; vStart = pos; pos += vLen; break
      }
      case 0x11: case 0x0D: { // bytes2 / utf8_2
        vLen = data[pos] | (data[pos + 1] << 8); pos += 2; vStart = pos; pos += vLen; break
      }
      case 0x15: case 0x16: case 0x17: { // containers: skip
        let depth = 1
        while (depth > 0 && pos < data.length) {
          const c = data[pos] & 0x1F
          pos++ // control byte
          const tf = data[pos - 1] & 0xE0
          if (tf === 0x20 && pos < data.length) pos++ // context tag
          if (c === 0x15 || c === 0x16 || c === 0x17) depth++
          else if (c === 0x18) depth--
          else {
            // Skip value bytes
            switch (c) {
              case 0x04: case 0x00: pos += 1; break
              case 0x05: case 0x01: pos += 2; break
              case 0x06: case 0x02: case 0x0A: pos += 4; break
              case 0x07: case 0x03: case 0x0B: pos += 8; break
              case 0x10: case 0x0C: { const l = data[pos++]; pos += l; break }
              case 0x11: case 0x0D: { const l = data[pos] | (data[pos + 1] << 8); pos += 2 + l; break }
            }
          }
        }
        continue // Don't store containers
      }
      case 0x18: continue // end of container
      default: return result
    }

    if (tag !== 0xFF) {
      result.set(tag, { type: elemType, value: data.subarray(vStart, vStart + vLen) })
    }
  }
  return result
}

// --- UDP helper ---

import dgram from 'dgram'

export function sendUDP(msg: Buffer, ip: string, timeoutMs = 5000): Promise<Buffer | null> {
  return new Promise((resolve) => {
    const sock = dgram.createSocket('udp4')
    const timeout = setTimeout(() => { sock.close(); resolve(null) }, timeoutMs)
    sock.on('message', (data) => {
      clearTimeout(timeout)
      sock.close()
      resolve(data)
    })
    sock.send(msg, MATTER_PORT, ip)
  })
}

// Parse response: extract protocol header fields and payload
export function parseResponse(data: Buffer): {
  msgCounter: number
  opcode: number
  exchangeId: number
  protocolId: number
  hasAck: boolean
  ackCounter: number
  payload: Buffer
} | null {
  if (data.length < 14) return null

  const flags = data[0]
  let hdrLen = 8
  if (flags & 0x04) hdrLen += 8
  if ((flags & 0x03) === 0x01) hdrLen += 8

  if (data.length < hdrLen + 6) return null

  const msgCounter = data.readUInt32LE(4)
  const exFlags = data[hdrLen]
  const opcode = data[hdrLen + 1]
  const exchangeId = data.readUInt16LE(hdrLen + 2)
  const protocolId = data.readUInt16LE(hdrLen + 4)
  const hasAck = (exFlags & EX_ACK) !== 0
  const payloadOffset = hdrLen + 6 + (hasAck ? 4 : 0)
  const ackCounter = hasAck ? data.readUInt32LE(hdrLen + 6) : 0

  return {
    msgCounter,
    opcode,
    exchangeId,
    protocolId,
    hasAck,
    ackCounter,
    payload: data.subarray(payloadOffset),
  }
}
