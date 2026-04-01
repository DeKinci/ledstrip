import { describe, it, expect } from 'vitest'
import { useDevice } from '@test/device.js'
import WebSocket from 'ws'
import { MicroProtoClient, MemoryStorage } from '@microproto/client'

describe('Stress — Rapid Updates', () => {
  it('handles 50 rapid property updates', async () => {
    const { client } = useDevice()

    let received = 0
    const listener = () => { received++ }
    client.on('property', listener)

    for (let i = 0; i < 50; i++) {
      client.setProperty('test/uint8', i % 256)
    }

    // Wait for broadcasts to settle
    await new Promise(r => setTimeout(r, 2000))
    client.off('property', listener)

    // Should receive at least some echoes (device broadcasts at ~15Hz)
    expect(received).toBeGreaterThan(0)
    console.log(`    Sent: 50, Received: ${received}`)
  })

  it('handles updates across multiple types', async () => {
    const { client } = useDevice()

    let received = 0
    const listener = () => { received++ }
    client.on('property', listener)

    for (let i = 0; i < 20; i++) {
      client.setProperty('test/bool', i % 2 === 0)
      client.setProperty('test/uint8', i * 10 % 256)
      client.setProperty('test/int32', i * -1000)
      client.setProperty('test/float', i * 0.5)
    }

    await new Promise(r => setTimeout(r, 2000))
    client.off('property', listener)

    expect(received).toBeGreaterThan(0)
    console.log(`    Sent: 80 (4 types × 20), Received: ${received}`)
  })
})

describe('Stress — Multi-Client', () => {
  it('two clients see each other updates', async () => {
    const ip = process.env.DEVICE_IP!
    const wsUrl = `ws://${ip}:81`

    const client2 = new MicroProtoClient(wsUrl, {
      reconnect: false,
      storage: new MemoryStorage(),
      webSocketFactory: (url: string) => new WebSocket(url) as any,
    })

    client2.connect()

    const ready = new Promise<void>((resolve, reject) => {
      const timeout = setTimeout(() => reject(new Error('Client 2 timeout')), 5000)
      client2.on('ready', () => { clearTimeout(timeout); resolve() })
    })

    await ready

    try {
      // Client 2 listens for brightness changes
      const received = new Promise<number>((resolve) => {
        client2.on('property', (id: number, name: string, value: any) => {
          if (name === 'test/uint8') resolve(value)
        })
      })

      // Client 1 (from useDevice) sends update
      const { client } = useDevice()
      const testValue = Math.floor(Math.random() * 200) + 10
      client.setProperty('test/uint8', testValue)

      const val = await Promise.race([
        received,
        new Promise<never>((_, reject) =>
          setTimeout(() => reject(new Error('Timeout')), 5000)),
      ])

      expect(val).toBe(testValue)
    } finally {
      client2.disconnect()
    }
  })
})

describe('Stress — Reconnect', () => {
  it('reconnects 3 times with successful handshake', async () => {
    const ip = process.env.DEVICE_IP!
    const wsUrl = `ws://${ip}:81`
    const successes: number[] = []

    for (let i = 0; i < 3; i++) {
      const start = Date.now()
      const client = new MicroProtoClient(wsUrl, {
        reconnect: false,
        storage: new MemoryStorage(),
        webSocketFactory: (url: string) => new WebSocket(url) as any,
      })

      const ready = new Promise<void>((resolve, reject) => {
        const timeout = setTimeout(() => reject(new Error(`Attempt ${i + 1} timeout`)), 5000)
        client.on('ready', () => { clearTimeout(timeout); resolve() })
      })

      client.connect()

      try {
        await ready
        successes.push(Date.now() - start)
        console.log(`    [${i + 1}/3] OK (${successes[successes.length - 1]}ms)`)
      } finally {
        client.disconnect()
      }

      await new Promise(r => setTimeout(r, 200))
    }

    expect(successes.length).toBe(3)
  })
})
