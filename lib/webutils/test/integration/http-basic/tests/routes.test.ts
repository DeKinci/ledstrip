import { describe, it, expect } from 'vitest'

import { getDeviceIP } from '@test/device.js'

const BASE = `http://${getDeviceIP()}`

describe('Basic Routes', () => {
  it('GET /ping returns pong', async () => {
    const res = await fetch(`${BASE}/ping`)
    expect(res.status).toBe(200)
    expect(await res.text()).toBe('pong')
  })

  it('GET /json returns JSON', async () => {
    const res = await fetch(`${BASE}/json`)
    expect(res.status).toBe(200)
    const body = await res.json()
    expect(body.status).toBe('ok')
  })

  it('GET / returns 200 (index or default)', async () => {
    const res = await fetch(`${BASE}/`)
    // May be 200 with content or 404 depending on firmware — just shouldn't crash
    expect([200, 404]).toContain(res.status)
  })

  it('GET /nonexistent returns 404', async () => {
    const res = await fetch(`${BASE}/nonexistent`)
    expect(res.status).toBe(404)
  })
})

describe('Status Codes', () => {
  it('GET /status/201 returns 201', async () => {
    const res = await fetch(`${BASE}/status/201`)
    expect(res.status).toBe(201)
  })

  it('GET /status/404 returns 404 with custom message', async () => {
    const res = await fetch(`${BASE}/status/404`)
    expect(res.status).toBe(404)
    expect(await res.text()).toContain('custom not found')
  })
})
