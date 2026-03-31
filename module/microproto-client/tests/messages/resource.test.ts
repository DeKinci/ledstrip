import { describe, it, expect } from 'vitest'
import {
  encodeResourceGet, encodeResourcePut, encodeResourceDelete,
  decodeResourceGetResponse, decodeResourcePutResponse, decodeResourceDeleteResponse,
  OPCODES,
} from '@microproto/client'

describe('encodeResourceGet', () => {
  it('encodes request', () => {
    const buf = encodeResourceGet(1, 5, 10)
    const bytes = new Uint8Array(buf)
    expect(bytes[0]).toBe(OPCODES.RESOURCE_GET)
    expect(bytes[1]).toBe(1) // requestId
    expect(bytes[2]).toBe(5) // propertyId
    expect(bytes[3]).toBe(10) // resourceId
  })
})

describe('encodeResourcePut', () => {
  it('encodes with body only', () => {
    const body = new Uint8Array([1, 2, 3])
    const buf = encodeResourcePut(0, 5, 0, { body })
    const bytes = new Uint8Array(buf)
    expect(bytes[0]).toBe(OPCODES.RESOURCE_PUT | (0x04 << 4)) // body flag
  })

  it('encodes with header and body', () => {
    const header = new Uint8Array([10])
    const body = new Uint8Array([20, 30])
    const buf = encodeResourcePut(0, 5, 1, { header, body })
    const bytes = new Uint8Array(buf)
    // flags: header(0x02) | body(0x04) = 0x06, shifted to upper nibble
    expect(bytes[0]).toBe(OPCODES.RESOURCE_PUT | (0x06 << 4))
  })

  it('encodes with no options', () => {
    const buf = encodeResourcePut(0, 5, 0)
    const bytes = new Uint8Array(buf)
    expect(bytes[0]).toBe(OPCODES.RESOURCE_PUT) // no flags
  })
})

describe('encodeResourceDelete', () => {
  it('encodes request', () => {
    const buf = encodeResourceDelete(2, 5, 10)
    const bytes = new Uint8Array(buf)
    expect(bytes[0]).toBe(OPCODES.RESOURCE_DELETE)
    expect(bytes[1]).toBe(2)
    expect(bytes[2]).toBe(5)
    expect(bytes[3]).toBe(10)
  })
})

describe('decodeResourceGetResponse', () => {
  it('decodes success', () => {
    const data = new Uint8Array([0x18, 1, 3, 10, 20, 30]) // requestId=1, bodyLen=3, body
    const result = decodeResourceGetResponse(data, 0x01)
    expect(result.success).toBe(true)
    expect(result.requestId).toBe(1)
    expect(result.data).toEqual(new Uint8Array([10, 20, 30]))
  })

  it('decodes error', () => {
    const msg = new TextEncoder().encode('fail')
    const data = new Uint8Array([0x38, 1, 5, msg.length, ...msg])
    const result = decodeResourceGetResponse(data, 0x03)
    expect(result.success).toBe(false)
    expect(result.requestId).toBe(1)
    expect(result.errorCode).toBe(5)
    expect(result.errorMessage).toBe('fail')
  })
})

describe('decodeResourcePutResponse', () => {
  it('decodes success with assigned resourceId', () => {
    const data = new Uint8Array([0x19, 1, 42]) // requestId=1, resourceId=42
    const result = decodeResourcePutResponse(data, 0x01)
    expect(result.success).toBe(true)
    expect(result.resourceId).toBe(42)
  })

  it('decodes error', () => {
    const msg = new TextEncoder().encode('err')
    const data = new Uint8Array([0x39, 1, 2, msg.length, ...msg])
    const result = decodeResourcePutResponse(data, 0x03)
    expect(result.success).toBe(false)
    expect(result.errorCode).toBe(2)
  })
})

describe('decodeResourceDeleteResponse', () => {
  it('decodes success', () => {
    const data = new Uint8Array([0x1a, 1])
    const result = decodeResourceDeleteResponse(data, 0x01)
    expect(result.success).toBe(true)
    expect(result.requestId).toBe(1)
  })

  it('decodes error', () => {
    const msg = new TextEncoder().encode('no')
    const data = new Uint8Array([0x3a, 1, 7, msg.length, ...msg])
    const result = decodeResourceDeleteResponse(data, 0x03)
    expect(result.success).toBe(false)
    expect(result.errorCode).toBe(7)
  })
})
