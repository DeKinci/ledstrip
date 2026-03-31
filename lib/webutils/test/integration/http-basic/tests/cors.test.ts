import { describe, it, expect } from 'vitest'
import { getDeviceIP } from '@test/device.js'

const BASE = `http://${getDeviceIP()}`

describe('CORS', () => {
  it('GET response includes CORS headers', async () => {
    const res = await fetch(`${BASE}/ping`)
    expect(res.headers.get('access-control-allow-origin')).toBe('*')
    expect(res.headers.get('access-control-allow-methods')).toBe('*')
    expect(res.headers.get('access-control-allow-methods'))
  })

  it('POST response includes CORS headers', async () => {
    const res = await fetch(`${BASE}/echo`, { method: 'POST', body: 'test' })
    expect(res.headers.get('access-control-allow-origin')).toBe('*')
  })

  it('404 response includes CORS headers', async () => {
    const res = await fetch(`${BASE}/nonexistent`)
    expect(res.status).toBe(404)
    expect(res.headers.get('access-control-allow-origin')).toBe('*')
  })

  it('OPTIONS preflight returns 204 with CORS headers', async () => {
    const res = await fetch(`${BASE}/ping`, { method: 'OPTIONS' })
    expect(res.status).toBe(204)
    expect(res.headers.get('access-control-allow-origin')).toBe('*')
    expect(res.headers.get('access-control-allow-methods')).toBe('*')
    expect(res.headers.get('access-control-allow-headers')).toBe('*')
    expect(res.headers.get('access-control-max-age')).toBe('86400')
  })

  it('OPTIONS on any path returns CORS headers', async () => {
    const res = await fetch(`${BASE}/nonexistent/path`, { method: 'OPTIONS' })
    expect(res.status).toBe(204)
    expect(res.headers.get('access-control-allow-origin')).toBe('*')
  })

  it('simulates browser cross-origin request flow', async () => {
    // 1. Preflight
    const preflight = await fetch(`${BASE}/json-echo`, {
      method: 'OPTIONS',
      headers: {
        'Origin': 'http://localhost:5173',
        'Access-Control-Request-Method': 'POST',
        'Access-Control-Request-Headers': 'Content-Type',
      },
    })
    expect(preflight.status).toBe(204)
    expect(preflight.headers.get('access-control-allow-origin')).toBe('*')

    // 2. Actual request
    const res = await fetch(`${BASE}/json-echo`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'Origin': 'http://localhost:5173',
      },
      body: JSON.stringify({ test: true }),
    })
    expect(res.status).toBe(200)
    expect(res.headers.get('access-control-allow-origin')).toBe('*')
    const body = await res.json()
    expect(body.test).toBe(true)
  })
})
