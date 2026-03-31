import { describe, it, expect } from 'vitest'
import { getDeviceIP } from '@test/device.js'

const BASE = `http://${getDeviceIP()}`

describe('Sequential Stress', () => {
  it('100 GET /ping in sequence', async () => {
    let failures = 0
    for (let i = 0; i < 100; i++) {
      try {
        const res = await fetch(`${BASE}/ping`)
        if (res.status !== 200) failures++
      } catch { failures++ }
    }
    expect(failures).toBe(0)
  }, 30000)

  it('50 POST /echo with varying body sizes', async () => {
    for (let i = 0; i < 50; i++) {
      const size = (i * 37) % 512 + 1 // Varying sizes 1-512 bytes
      const body = String.fromCharCode(65 + (i % 26)).repeat(size)
      const res = await fetch(`${BASE}/echo`, {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body,
      })
      expect(res.status).toBe(200)
      const text = await res.text()
      expect(text.length).toBe(size)
    }
  }, 30000)

  it('alternating GET and POST', async () => {
    for (let i = 0; i < 40; i++) {
      if (i % 2 === 0) {
        const res = await fetch(`${BASE}/ping`)
        expect(res.status).toBe(200)
      } else {
        const res = await fetch(`${BASE}/echo`, { method: 'POST', body: `msg-${i}` })
        expect(res.status).toBe(200)
        expect(await res.text()).toBe(`msg-${i}`)
      }
    }
  }, 30000)
})

describe('Parallel Stress', () => {
  it('10 parallel GET', async () => {
    const results = await Promise.all(
      Array.from({ length: 10 }, () =>
        fetch(`${BASE}/ping`).then(async r => ({ ok: r.status === 200, body: await r.text() }))
      )
    )
    for (const r of results) {
      expect(r.ok).toBe(true)
      expect(r.body).toBe('pong')
    }
  })

  it('5 parallel POST with unique bodies', async () => {
    const results = await Promise.all(
      Array.from({ length: 5 }, (_, i) =>
        fetch(`${BASE}/echo`, { method: 'POST', body: `parallel-${i}` })
          .then(async r => ({ status: r.status, body: await r.text() }))
      )
    )
    for (let i = 0; i < 5; i++) {
      expect(results[i].status).toBe(200)
      expect(results[i].body).toBe(`parallel-${i}`)
    }
  })

  it('20 parallel to mixed routes', async () => {
    const routes = ['/ping', '/json', '/user/1', '/query?a=x', '/html']
    const results = await Promise.all(
      Array.from({ length: 20 }, (_, i) =>
        fetch(`${BASE}${routes[i % routes.length]}`)
          .then(async r => ({ status: r.status }))
          .catch(() => ({ status: 0 }))
      )
    )
    const ok = results.filter(r => r.status === 200).length
    console.log(`    ${ok}/20 parallel requests succeeded`)
    expect(ok).toBeGreaterThanOrEqual(15) // Allow some failures under load
  })
})

describe('Latency', () => {
  it('GET /ping avg < 50ms', async () => {
    await fetch(`${BASE}/ping`) // warmup
    const times: number[] = []
    for (let i = 0; i < 30; i++) {
      const start = Date.now()
      await fetch(`${BASE}/ping`)
      times.push(Date.now() - start)
    }
    const avg = times.reduce((a, b) => a + b, 0) / times.length
    const max = Math.max(...times)
    const p95 = times.sort((a, b) => a - b)[Math.floor(times.length * 0.95)]
    console.log(`    avg=${avg.toFixed(0)}ms  p95=${p95}ms  max=${max}ms`)
    expect(avg).toBeLessThan(50)
  })

  it('POST /echo avg < 50ms', async () => {
    await fetch(`${BASE}/echo`, { method: 'POST', body: 'warmup' })
    const times: number[] = []
    for (let i = 0; i < 20; i++) {
      const start = Date.now()
      await fetch(`${BASE}/echo`, { method: 'POST', body: 'test' })
      times.push(Date.now() - start)
    }
    const avg = times.reduce((a, b) => a + b, 0) / times.length
    console.log(`    avg=${avg.toFixed(0)}ms  max=${Math.max(...times)}ms`)
    expect(avg).toBeLessThan(50)
  })
})
