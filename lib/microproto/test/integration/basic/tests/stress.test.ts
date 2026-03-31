import { describe, it, expect } from 'vitest'
import { useDevice } from '@test/device.js'

describe('Stress', () => {
  it('handles rapid property updates (20/sec for 3s)', async () => {
    const { client } = useDevice()

    const duration = 3000
    const interval = 50 // 20 updates/sec
    let sent = 0
    let received = 0
    const latencies: number[] = []

    const listener = () => { received++ }
    client.on('property', listener)

    const start = Date.now()
    while (Date.now() - start < duration) {
      const value = (sent * 10) % 256
      client.setProperty('brightness', value)
      sent++
      await new Promise(r => setTimeout(r, interval))
    }

    // Wait a bit for remaining broadcasts
    await new Promise(r => setTimeout(r, 500))
    client.off('property', listener)

    console.log(`    Sent: ${sent}, Received: ${received}`)
    expect(sent).toBeGreaterThan(0)
    // We should receive at least some broadcasts back
    expect(received).toBeGreaterThan(0)
  })
})
