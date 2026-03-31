import { describe, it, expect } from 'vitest'
import { getDeviceIP } from '@test/device.js'

const BASE = `http://${getDeviceIP()}`

describe('Content-Encoding Headers', () => {
  // Note: test firmware doesn't serve brotli-compressed resources,
  // but we verify the framework works by checking regular responses
  // don't have Content-Encoding set.

  it('regular response has no Content-Encoding', async () => {
    const res = await fetch(`${BASE}/ping`)
    expect(res.headers.get('content-encoding')).toBeNull()
  })

  it('JSON response has correct Content-Type', async () => {
    const res = await fetch(`${BASE}/json`)
    expect(res.headers.get('content-type')).toContain('application/json')
    expect(res.headers.get('content-encoding')).toBeNull()
  })

  it('HTML response has correct Content-Type', async () => {
    const res = await fetch(`${BASE}/html`)
    expect(res.headers.get('content-type')).toContain('text/html')
  })
})
