import { describe, it, expect } from 'vitest'
import { useDevice } from '@test/device.js'

describe('Gaslamp Properties', () => {
  it('has brightness property (UINT8, 0-255)', () => {
    const { client } = useDevice()
    const schema = client.getPropertySchema('brightness')
    expect(schema).toBeDefined()
    expect(schema!.typeId).toBe(0x03) // UINT8
    expect(schema!.constraints.hasMin).toBe(true)
    expect(schema!.constraints.min).toBe(0)
    expect(schema!.constraints.hasMax).toBe(true)
    expect(schema!.constraints.max).toBe(255)
  })

  it('has enabled toggle', () => {
    const { client } = useDevice()
    const schema = client.getPropertySchema('enabled')
    expect(schema).toBeDefined()
    expect(schema!.typeId).toBe(0x01) // BOOL
  })

  it('can set brightness and receives echo', async () => {
    const { client } = useDevice()

    const received = new Promise<number>((resolve) => {
      client.on('property', (_id: number, name: string, value: any) => {
        if (name === 'brightness') resolve(value)
      })
    })

    client.setProperty('brightness', 100)

    const value = await Promise.race([
      received,
      new Promise<never>((_, reject) => setTimeout(() => reject(new Error('Timeout')), 5000)),
    ])

    expect(value).toBe(100)
  })
})
