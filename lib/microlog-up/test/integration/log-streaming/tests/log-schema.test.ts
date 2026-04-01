import { describe, it, expect } from 'vitest'
import { useDevice } from '@test/device.js'
import { TYPES } from '@microproto/client'

describe('Log Properties — Schema', () => {
  it('has sys/errorLog stream property', () => {
    const { client } = useDevice()
    const schema = client.getPropertySchema('sys/errorLog')
    expect(schema).toBeDefined()
    expect(schema!.typeId).toBe(TYPES.STREAM)
    expect(schema!.readonly).toBe(true)
    expect(schema!.persistent).toBe(true)
  })

  it('has sys/logStream stream property', () => {
    const { client } = useDevice()
    const schema = client.getPropertySchema('sys/logStream')
    expect(schema).toBeDefined()
    expect(schema!.typeId).toBe(TYPES.STREAM)
    expect(schema!.readonly).toBe(true)
    expect(schema!.hidden).toBe(true)
  })

  it('log stream has OBJECT element type (LogEntry)', () => {
    const { client } = useDevice()
    const schema = client.getPropertySchema('sys/logStream')!
    expect(schema.elementTypeId).toBe(TYPES.OBJECT)
    expect(schema.elementTypeDef).toBeDefined()
  })

  it('errorLog has OBJECT element type (LogEntry)', () => {
    const { client } = useDevice()
    const schema = client.getPropertySchema('sys/errorLog')!
    expect(schema.elementTypeId).toBe(TYPES.OBJECT)
  })
})
