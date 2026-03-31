import { describe, it, expect } from 'vitest'
import { encodeRpcRequest, decodeRpcResponse, decodeRpcRequest, OPCODES, TYPES } from '@microproto/client'

describe('encodeRpcRequest', () => {
  it('encodes with needs_response', () => {
    const buf = encodeRpcRequest(1, 0, true)
    const bytes = new Uint8Array(buf)
    // opcode=5, flags=0x02 (needs_response) → header = 0x25
    expect(bytes[0]).toBe(0x25)
    expect(bytes[1]).toBe(1) // function id
    expect(bytes[2]).toBe(0) // call id
    expect(bytes.length).toBe(3)
  })

  it('encodes fire-and-forget (no call id)', () => {
    const buf = encodeRpcRequest(2, null, false)
    const bytes = new Uint8Array(buf)
    expect(bytes[0]).toBe(0x05) // no flags
    expect(bytes[1]).toBe(2)
    expect(bytes.length).toBe(2)
  })

  it('encodes with params', () => {
    const params = new Uint8Array([42, 0xff])
    const buf = encodeRpcRequest(1, 0, true, params)
    const bytes = new Uint8Array(buf)
    expect(bytes[0]).toBe(0x25)
    expect(bytes[1]).toBe(1)  // func id
    expect(bytes[2]).toBe(0)  // call id
    expect(bytes[3]).toBe(42)
    expect(bytes[4]).toBe(0xff)
  })
})

describe('decodeRpcResponse', () => {
  it('decodes success with return value', () => {
    // flags=7: is_response | success | has_return_value → header = 0x75
    const data = new Uint8Array([0x75, 0, 42])
    const result = decodeRpcResponse(data, 0x07, (callId) => ({ typeId: TYPES.UINT8 }))
    expect(result.success).toBe(true)
    expect(result.callId).toBe(0)
    expect(result.value).toBe(42)
  })

  it('decodes success without return value', () => {
    // flags=3: is_response | success → header = 0x35
    const data = new Uint8Array([0x35, 0])
    const result = decodeRpcResponse(data, 0x03)
    expect(result.success).toBe(true)
    expect(result.value).toBeUndefined()
  })

  it('decodes error response', () => {
    const msg = new TextEncoder().encode('bad')
    // flags=1: is_response only
    const data = new Uint8Array([0x15, 0, 3, msg.length, ...msg])
    const result = decodeRpcResponse(data, 0x01)
    expect(result.success).toBe(false)
    expect(result.errorCode).toBe(3)
    expect(result.errorMessage).toBe('bad')
  })

  it('returns raw data when no return type def', () => {
    const data = new Uint8Array([0x75, 5, 10, 20])
    const result = decodeRpcResponse(data, 0x07)
    expect(result.success).toBe(true)
    expect(result.data).toEqual(new Uint8Array([10, 20]))
  })
})

describe('decodeRpcRequest', () => {
  it('decodes request with needs_response', () => {
    const data = new Uint8Array([0x25, 3, 7, 0xaa]) // funcId=3, callId=7, extra data
    const result = decodeRpcRequest(data, 0x02)
    expect(result.functionId).toBe(3)
    expect(result.callId).toBe(7)
    expect(result.needsResponse).toBe(true)
    expect(result.data).toEqual(new Uint8Array([0xaa]))
  })

  it('decodes fire-and-forget request', () => {
    const data = new Uint8Array([0x05, 2])
    const result = decodeRpcRequest(data, 0x00)
    expect(result.functionId).toBe(2)
    expect(result.callId).toBeNull()
    expect(result.needsResponse).toBe(false)
  })
})
