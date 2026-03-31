import { describe, it, expect } from 'vitest'
import { getDeviceIP } from '@test/device.js'

const BASE = `http://${getDeviceIP()}`

describe('Edge Cases', () => {
  it('very long URL path', async () => {
    const longPath = '/a'.repeat(200)
    const res = await fetch(`${BASE}${longPath}`)
    // Should return 404, not crash
    expect([400, 404, 414]).toContain(res.status)
  })

  it('long query string', async () => {
    const longQuery = `?x=${'a'.repeat(500)}`
    const res = await fetch(`${BASE}/query${longQuery}`)
    // Should not crash
    expect(res.status).toBeGreaterThanOrEqual(200)
  })

  it('request with many headers', async () => {
    const headers: Record<string, string> = {}
    for (let i = 0; i < 20; i++) {
      headers[`X-Header-${i}`] = `value-${i}`
    }
    const res = await fetch(`${BASE}/ping`, { headers })
    expect(res.status).toBe(200)
  })

  it('Connection: close header', async () => {
    const res = await fetch(`${BASE}/ping`, {
      headers: { Connection: 'close' },
    })
    expect(res.status).toBe(200)
    expect(await res.text()).toBe('pong')
  })

  it('rapid connect-disconnect cycle', async () => {
    for (let i = 0; i < 20; i++) {
      const controller = new AbortController()
      try {
        const res = await fetch(`${BASE}/ping`, {
          signal: controller.signal,
        })
        expect(res.status).toBe(200)
      } catch {
        // AbortError is ok
      }
    }
    // Verify server still works after rapid cycles
    const res = await fetch(`${BASE}/ping`)
    expect(res.status).toBe(200)
    expect(await res.text()).toBe('pong')
  })

  it('handles concurrent connections to different routes', async () => {
    const routes = ['/ping', '/json', '/user/1', '/user/2', '/html', '/large']
    const results = await Promise.all(
      routes.map(r => fetch(`${BASE}${r}`).then(res => res.status))
    )
    for (const status of results) {
      expect(status).toBe(200)
    }
  })

  it('works after /slow blocks for 500ms', async () => {
    // Start a slow request, then verify fast request still works
    const slowPromise = fetch(`${BASE}/slow`)
    // Give it a moment to start
    await new Promise(r => setTimeout(r, 100))
    // This should either wait or work depending on server model
    const fastRes = await fetch(`${BASE}/ping`)
    const slowRes = await slowPromise
    expect(slowRes.status).toBe(200)
    expect(fastRes.status).toBe(200)
  })
})

describe('Content Integrity', () => {
  it('4KB response is byte-accurate', async () => {
    const res = await fetch(`${BASE}/large`)
    const text = await res.text()
    expect(text.length).toBe(4096)
    // Every 16 chars should be the same pattern
    for (let i = 0; i < 256; i++) {
      expect(text.substring(i * 16, i * 16 + 16)).toBe('0123456789ABCDEF')
    }
  })

  it('echo preserves exact bytes', async () => {
    // Binary-like content
    const body = Array.from({ length: 256 }, (_, i) => String.fromCharCode(i % 128)).join('')
    const res = await fetch(`${BASE}/echo`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/octet-stream' },
      body,
    })
    expect(await res.text()).toBe(body)
  })

  it('JSON round-trip preserves types', async () => {
    const payload = { str: 'hello', num: 3.14, bool: true, nil: null, arr: [1, 2] }
    const res = await fetch(`${BASE}/json-echo`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload),
    })
    const json = await res.json()
    expect(json.str).toBe('hello')
    expect(json.num).toBeCloseTo(3.14)
    expect(json.bool).toBe(true)
    expect(json.nil).toBeNull()
    expect(json.arr).toEqual([1, 2])
    expect(json.echoed).toBe(true)
  })
})
