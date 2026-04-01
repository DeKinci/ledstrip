import { describe, it, expect } from 'vitest'
import { useDevice } from '@test/device.js'

/**
 * Verify that the local WebSocket transport works alongside the gateway client.
 * The gateway client should not interfere with local MicroProto connections.
 */
describe('Local WS alongside Gateway', () => {
  it('connects via local WebSocket', () => {
    const { client } = useDevice()
    expect(client.isConnected()).toBe(true)
  })

  it('receives schema with properties', () => {
    const { client } = useDevice()
    expect(client.properties.size).toBeGreaterThan(0)
    expect(client.propertyByName.has('brightness')).toBe(true)
    expect(client.propertyByName.has('enabled')).toBe(true)
  })

  it('can update brightness via local WS', async () => {
    const { client } = useDevice()

    const received = new Promise<number>((resolve) => {
      client.on('property', (id: number, name: string, value: any) => {
        if (name === 'brightness') resolve(value)
      })
    })

    const newValue = Math.floor(Math.random() * 200) + 10
    client.setProperty('brightness', newValue)

    const echoValue = await Promise.race([
      received,
      new Promise<never>((_, reject) => setTimeout(() => reject(new Error('Timeout')), 5000)),
    ])

    expect(echoValue).toBe(newValue)
  })
})
