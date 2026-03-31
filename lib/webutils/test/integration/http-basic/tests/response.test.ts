import { describe, it, expect } from 'vitest'
import { getDeviceIP } from '@test/device.js'

const BASE = `http://${getDeviceIP()}`

describe('Response Formats', () => {
  it('/ping returns text/plain', async () => {
    const res = await fetch(`${BASE}/ping`)
    expect(res.headers.get('content-type')).toContain('text/plain')
  })

  it('/json returns application/json', async () => {
    const res = await fetch(`${BASE}/json`)
    expect(res.headers.get('content-type')).toContain('application/json')
  })

  it('/html returns text/html', async () => {
    const res = await fetch(`${BASE}/html`)
    expect(res.status).toBe(200)
    expect(res.headers.get('content-type')).toContain('text/html')
    expect(await res.text()).toContain('<h1>Hello</h1>')
  })

  it('/void returns 204 with no body', async () => {
    const res = await fetch(`${BASE}/void`, { method: 'POST' })
    expect(res.status).toBe(204)
  })

  it('/status/500 returns 500', async () => {
    const res = await fetch(`${BASE}/status/500`)
    expect(res.status).toBe(500)
    expect(await res.text()).toContain('test error')
  })

  it('/large returns 4KB response', async () => {
    const res = await fetch(`${BASE}/large`)
    expect(res.status).toBe(200)
    const text = await res.text()
    expect(text.length).toBe(4096)
    // Verify content pattern
    expect(text.startsWith('0123456789ABCDEF')).toBe(true)
  })
})
