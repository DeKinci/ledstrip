import { describe, it, expect } from 'vitest'
import { useDevice } from '@test/device.js'

describe('Property Sync', () => {
  it('sends UINT8 property update and receives echo', async () => {
    const { client } = useDevice()

    const received = new Promise<number>((resolve) => {
      client.on('property', (id: number, name: string, value: any) => {
        if (name === 'brightness') resolve(value)
      })
    })

    const newValue = Math.floor(Math.random() * 200) + 10
    const ok = client.setProperty('brightness', newValue)
    expect(ok).toBe(true)

    const echoValue = await Promise.race([
      received,
      new Promise<never>((_, reject) => setTimeout(() => reject(new Error('Timeout waiting for property echo')), 5000)),
    ])

    expect(echoValue).toBe(newValue)
  })

  it('sends BOOL property update', async () => {
    const { client } = useDevice()

    const received = new Promise<boolean>((resolve) => {
      client.on('property', (id: number, name: string, value: any) => {
        if (name === 'enabled') resolve(value)
      })
    })

    const current = client.getProperty('enabled')
    client.setProperty('enabled', !current)

    const echoValue = await Promise.race([
      received,
      new Promise<never>((_, reject) => setTimeout(() => reject(new Error('Timeout')), 5000)),
    ])

    expect(echoValue).toBe(!current)
  })

  it('sends FLOAT32 property update', async () => {
    const { client } = useDevice()

    const received = new Promise<number>((resolve) => {
      client.on('property', (id: number, name: string, value: any) => {
        if (name === 'speed') resolve(value)
      })
    })

    client.setProperty('speed', 3.5)

    const echoValue = await Promise.race([
      received,
      new Promise<never>((_, reject) => setTimeout(() => reject(new Error('Timeout')), 5000)),
    ])

    expect(Math.abs(echoValue - 3.5)).toBeLessThan(0.01)
  })

  it('sends INT32 property update', async () => {
    const { client } = useDevice()

    const received = new Promise<number>((resolve) => {
      client.on('property', (id: number, name: string, value: any) => {
        if (name === 'count') resolve(value)
      })
    })

    client.setProperty('count', -42)

    const echoValue = await Promise.race([
      received,
      new Promise<never>((_, reject) => setTimeout(() => reject(new Error('Timeout')), 5000)),
    ])

    expect(echoValue).toBe(-42)
  })

  it('rejects readonly property update', () => {
    const { client } = useDevice()

    // Find a readonly property if any, otherwise skip
    for (const [, prop] of client.properties) {
      if (prop.readonly) {
        const result = client.setProperty(prop.name, 0)
        expect(result).toBe(false)
        return
      }
    }
    // No readonly properties in this firmware, that's ok
  })
})
