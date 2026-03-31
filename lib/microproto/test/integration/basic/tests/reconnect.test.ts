import { describe, it, expect } from 'vitest'
import WebSocket from 'ws'
import { MicroProtoClient, MemoryStorage } from '@microproto/client'

describe('Reconnect', () => {
  it('completes handshake on 5 rapid reconnections', async () => {
    const ip = process.env.DEVICE_IP!
    const wsUrl = `ws://${ip}:81`
    const successes: number[] = []

    for (let i = 0; i < 5; i++) {
      const start = Date.now()

      const client = new MicroProtoClient(wsUrl, {
        reconnect: false,
        storage: new MemoryStorage(),
        webSocketFactory: (url: string) => new WebSocket(url) as any,
      })

      const ready = new Promise<void>((resolve, reject) => {
        const timeout = setTimeout(() => reject(new Error(`Attempt ${i + 1} timeout`)), 5000)
        client.on('ready', () => { clearTimeout(timeout); resolve() })
        client.on('error', (e: any) => { clearTimeout(timeout); reject(new Error(e.message)) })
      })

      client.connect()

      try {
        await ready
        const elapsed = Date.now() - start
        successes.push(elapsed)
        console.log(`    [${i + 1}/5] OK (${elapsed}ms)`)
      } finally {
        client.disconnect()
      }

      // Small delay between connections
      await new Promise(r => setTimeout(r, 100))
    }

    expect(successes.length).toBe(5)

    const avg = successes.reduce((a, b) => a + b, 0) / successes.length
    console.log(`    Average handshake: ${avg.toFixed(0)}ms`)
  }, 30000)
})
