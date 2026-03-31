import { describe, it, expect } from 'vitest'
import { decodeError, ERROR_CODES } from '@microproto/client'

describe('decodeError', () => {
  it('decodes error code and message', () => {
    const msgBytes = new TextEncoder().encode('Bad value')
    const data = new Uint8Array([
      0x07,           // ERROR opcode
      0x05, 0x00,     // code 5 (VALIDATION_FAILED) LE
      msgBytes.length, // varint message length
      ...msgBytes,
    ])

    const result = decodeError(data, 0)
    expect(result.code).toBe(ERROR_CODES.VALIDATION_FAILED)
    expect(result.message).toBe('Bad value')
    expect(result.schemaMismatch).toBe(false)
  })

  it('decodes schema mismatch flag', () => {
    const data = new Uint8Array([
      0x17,       // ERROR with schema_mismatch flag
      0x02, 0x00, // code 2
      0x02,       // message length
      0x4f, 0x4b, // "OK"
    ])

    const result = decodeError(data, 0x01)
    expect(result.schemaMismatch).toBe(true)
    expect(result.code).toBe(2)
    expect(result.message).toBe('OK')
  })

  it('decodes empty message', () => {
    const data = new Uint8Array([0x07, 0x01, 0x00, 0x00])
    const result = decodeError(data, 0)
    expect(result.code).toBe(1)
    expect(result.message).toBe('')
  })
})
