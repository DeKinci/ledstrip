import { describe, it, expect } from 'vitest'
import { useDevice } from '@test/device.js'

describe('Readonly Properties', () => {
  it('schema marks property as readonly', () => {
    const { client } = useDevice()
    const schema = client.getPropertySchema('test/readonly')!
    expect(schema.readonly).toBe(true)
  })

  it('receives readonly initial value', () => {
    const { client } = useDevice()
    expect(client.getProperty('test/readonly')).toBe(42)
  })

  it('rejects write to readonly property', () => {
    const { client } = useDevice()
    const result = client.setProperty('test/readonly', 99)
    expect(result).toBe(false)
  })
})

describe('Hidden Properties', () => {
  it('schema marks property as hidden', () => {
    const { client } = useDevice()
    const schema = client.getPropertySchema('test/hidden')!
    expect(schema.hidden).toBe(true)
  })

  it('hidden property still syncs value', () => {
    const { client } = useDevice()
    // Hidden properties are not shown in UI but still sync
    expect(client.getProperty('test/hidden')).toBe(999)
  })
})

describe('Stream Properties', () => {
  it('schema has STREAM type', () => {
    const { client } = useDevice()
    const schema = client.getPropertySchema('test/events')!
    expect(schema.typeId).toBe(0x25) // TYPE_STREAM
    expect(schema.readonly).toBe(true)
  })

  it('stream has OBJECT element type with field names', () => {
    const { client } = useDevice()
    const schema = client.getPropertySchema('test/events')!
    expect(schema.elementTypeId).toBe(0x22) // TYPE_OBJECT
    expect(schema.elementTypeDef).toBeDefined()
  })
})

describe('Property Descriptions', () => {
  it('properties have descriptions', () => {
    const { client } = useDevice()
    const cases: [string, string][] = [
      ['test/bool', 'Boolean test property'],
      ['test/uint8', 'Unsigned 8-bit'],
      ['test/float', '32-bit float'],
      ['test/readonly', 'Readonly value'],
      ['test/rgb', 'RGB color'],
      ['test/point', '3D point'],
    ]
    for (const [name, desc] of cases) {
      const schema = client.getPropertySchema(name)!
      expect(schema.description).toBe(desc)
    }
  })
})
