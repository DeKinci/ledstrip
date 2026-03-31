import { describe, it, expect } from 'vitest'
import { getDeviceIP } from '@test/device.js'

const BASE = `http://${getDeviceIP()}`

describe('ETag', () => {
  it('response includes ETag header', async () => {
    const res = await fetch(`${BASE}/etag/static`)
    expect(res.status).toBe(200)
    expect(res.headers.get('etag')).toBe('"abc123"')
    expect(await res.text()).toBe('static content')
  })

  it('returns 304 when If-None-Match matches', async () => {
    const res = await fetch(`${BASE}/etag/static`, {
      headers: { 'If-None-Match': '"abc123"' },
    })
    expect(res.status).toBe(304)
  })

  it('returns 200 when If-None-Match does not match', async () => {
    const res = await fetch(`${BASE}/etag/static`, {
      headers: { 'If-None-Match': '"wrong"' },
    })
    expect(res.status).toBe(200)
    expect(await res.text()).toBe('static content')
  })

  it('304 response includes ETag but no body', async () => {
    const res = await fetch(`${BASE}/etag/static`, {
      headers: { 'If-None-Match': '"abc123"' },
    })
    expect(res.status).toBe(304)
    expect(res.headers.get('etag')).toBe('"abc123"')
  })

  it('works with different etag values per route', async () => {
    const res1 = await fetch(`${BASE}/etag/static`)
    expect(res1.headers.get('etag')).toBe('"abc123"')

    const res2 = await fetch(`${BASE}/etag/versioned`)
    expect(res2.headers.get('etag')).toBe('"v1hash"')

    // Cross-check: static etag should NOT match versioned
    const res3 = await fetch(`${BASE}/etag/versioned`, {
      headers: { 'If-None-Match': '"abc123"' },
    })
    expect(res3.status).toBe(200)
  })

  it('includes Cache-Control: no-cache with ETag', async () => {
    const res = await fetch(`${BASE}/etag/static`)
    expect(res.headers.get('cache-control')).toBe('no-cache')
  })

  it('non-etag routes have no ETag header', async () => {
    const res = await fetch(`${BASE}/ping`)
    expect(res.headers.get('etag')).toBeNull()
    expect(res.headers.get('cache-control')).toBeNull()
  })

  it('304 performance: simulates browser cache revalidation', async () => {
    // First request: get content + etag
    const initial = await fetch(`${BASE}/etag/static`)
    const etag = initial.headers.get('etag')!
    expect(etag).toBeTruthy()

    // Subsequent requests with If-None-Match: should be fast 304s
    const times: number[] = []
    for (let i = 0; i < 20; i++) {
      const start = Date.now()
      const res = await fetch(`${BASE}/etag/static`, {
        headers: { 'If-None-Match': etag },
      })
      times.push(Date.now() - start)
      expect(res.status).toBe(304)
    }
    const avg = times.reduce((a, b) => a + b, 0) / times.length
    console.log(`    304 avg: ${avg.toFixed(0)}ms`)
    expect(avg).toBeLessThan(500)
  })
})
