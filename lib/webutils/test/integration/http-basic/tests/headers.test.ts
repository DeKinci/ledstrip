import { describe, it, expect } from 'vitest'

import { getDeviceIP } from '@test/device.js'

const BASE = `http://${getDeviceIP()}`

describe('Headers', () => {
  it('receives custom header', async () => {
    const res = await fetch(`${BASE}/headers`, {
      headers: { 'X-Test': 'my-value' },
    })
    expect(res.status).toBe(200)
    const body = await res.json()
    expect(body['x-test']).toBe('my-value')
  })

  it('receives host header', async () => {
    const res = await fetch(`${BASE}/headers`)
    const body = await res.json()
    expect(body.host).toBeDefined()
  })
})
