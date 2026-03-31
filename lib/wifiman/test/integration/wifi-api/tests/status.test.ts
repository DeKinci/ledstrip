import { describe, it, expect } from 'vitest'
import { getDeviceIP } from '@test/device.js'

const BASE = `http://${getDeviceIP()}`

describe('Status API', () => {
  it('GET /wifiman/status returns JSON with required fields', async () => {
    const res = await fetch(`${BASE}/wifiman/status`)
    expect(res.status).toBe(200)
    const json = await res.json()

    expect(json).toHaveProperty('state')
    expect(json).toHaveProperty('connected')
    expect(json).toHaveProperty('ip')
    expect(json).toHaveProperty('apMode')
    expect(typeof json.connected).toBe('boolean')
    expect(typeof json.apMode).toBe('boolean')
  })

  it('reports connected state', async () => {
    const res = await fetch(`${BASE}/wifiman/status`)
    const json = await res.json()
    // If we can reach it, it should be connected
    expect(json.connected).toBe(true)
    expect(json.ip).toBeTruthy()
  })
})
