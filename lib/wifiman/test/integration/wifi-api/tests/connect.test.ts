import { describe, it, expect } from 'vitest'
import { getDeviceIP } from '@test/device.js'

const BASE = `http://${getDeviceIP()}`

describe('Connect API', () => {
  it('POST /wifiman/connect triggers connection', async () => {
    const res = await fetch(`${BASE}/wifiman/connect`, { method: 'POST' })
    expect(res.status).toBe(200)
    const json = await res.json()
    expect(json.success).toBe(true)
  })

  it('device remains reachable after connect request', async () => {
    await fetch(`${BASE}/wifiman/connect`, { method: 'POST' })
    // Wait a moment for connection logic
    await new Promise(r => setTimeout(r, 1000))
    // Should still be reachable
    const res = await fetch(`${BASE}/ping`)
    expect(res.status).toBe(200)
  })
})
