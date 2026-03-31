import { describe, it, expect } from 'vitest'

import { getDeviceIP } from '@test/device.js'

const BASE = `http://${getDeviceIP()}`

describe('Reliability', () => {
  it('handles 50 sequential requests', async () => {
    for (let i = 0; i < 50; i++) {
      const res = await fetch(`${BASE}/ping`)
      expect(res.status).toBe(200)
      expect(await res.text()).toBe('pong')
    }
  }, 30000)

  it('handles 10 parallel requests', async () => {
    const promises = Array.from({ length: 10 }, () =>
      fetch(`${BASE}/ping`).then(async r => ({ status: r.status, body: await r.text() }))
    )
    const results = await Promise.all(promises)
    for (const r of results) {
      expect(r.status).toBe(200)
      expect(r.body).toBe('pong')
    }
  })

  it('/slow responds within 5s', async () => {
    const start = Date.now()
    const res = await fetch(`${BASE}/slow`)
    const elapsed = Date.now() - start
    expect(res.status).toBe(200)
    expect(await res.text()).toBe('done')
    expect(elapsed).toBeGreaterThan(400)
    expect(elapsed).toBeLessThan(5000)
  })

  it('latency under 100ms for /ping', async () => {
    // Warmup (first request after flash can be slow)
    for (let i = 0; i < 3; i++) await fetch(`${BASE}/ping`)

    const times: number[] = []
    for (let i = 0; i < 20; i++) {
      const start = Date.now()
      await fetch(`${BASE}/ping`)
      times.push(Date.now() - start)
    }

    const avg = times.reduce((a, b) => a + b, 0) / times.length
    console.log(`    /ping avg: ${avg.toFixed(0)}ms, max: ${Math.max(...times)}ms`)
    expect(avg).toBeLessThan(100)
  })
})
