import { describe, it, expect } from 'vitest'
import { useDevice } from '@test/device.js'

describe('MicroProto Handshake', () => {
  it('connects and receives HELLO response', () => {
    const { client } = useDevice()
    expect(client.isConnected()).toBe(true)
    expect(client.sessionId).not.toBeNull()
  })

  it('receives schema with expected properties', () => {
    const { client } = useDevice()
    expect(client.properties.size).toBeGreaterThan(0)

    // Verify known properties exist
    expect(client.propertyByName.has('brightness')).toBe(true)
    expect(client.propertyByName.has('enabled')).toBe(true)
    expect(client.propertyByName.has('speed')).toBe(true)
    expect(client.propertyByName.has('count')).toBe(true)
  })

  it('receives initial property values', () => {
    const { client } = useDevice()

    // Values should be set (not null) after ready
    const brightness = client.getProperty('brightness')
    expect(brightness).not.toBeUndefined()
    expect(typeof brightness).toBe('number')

    const enabled = client.getProperty('enabled')
    expect(typeof enabled).toBe('boolean')
  })

  it('schema has correct type IDs', () => {
    const { client } = useDevice()

    const brightness = client.getPropertySchema('brightness')!
    expect(brightness.typeId).toBe(0x03) // UINT8

    const enabled = client.getPropertySchema('enabled')!
    expect(enabled.typeId).toBe(0x01) // BOOL

    const speed = client.getPropertySchema('speed')!
    expect(speed.typeId).toBe(0x05) // FLOAT32

    const count = client.getPropertySchema('count')!
    expect(count.typeId).toBe(0x04) // INT32
  })

  it('schema has constraints', () => {
    const { client } = useDevice()

    const brightness = client.getPropertySchema('brightness')!
    expect(brightness.constraints.hasMin).toBe(true)
    expect(brightness.constraints.min).toBe(0)
    expect(brightness.constraints.hasMax).toBe(true)
    expect(brightness.constraints.max).toBe(255)

    const speed = client.getPropertySchema('speed')!
    expect(speed.constraints.hasMin).toBe(true)
    expect(speed.constraints.hasMax).toBe(true)
  })

  it('schema has UI hints', () => {
    const { client } = useDevice()

    const brightness = client.getPropertySchema('brightness')!
    expect(brightness.ui.color).toBe('amber')
    expect(brightness.ui.widget).toBe(1) // SLIDER

    const enabled = client.getPropertySchema('enabled')!
    expect(enabled.ui.color).toBe('lime')
    expect(enabled.ui.widget).toBe(2) // TOGGLE
  })
})
