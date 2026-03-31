import { describe, it, expect } from 'vitest'
import { getDeviceIP } from '@test/device.js'

const BASE = `http://${getDeviceIP()}`

describe('Scan API', () => {
  it('GET /wifiman/scan returns JSON', async () => {
    const res = await fetch(`${BASE}/wifiman/scan`)
    expect(res.status).toBe(200)
    const json = await res.json()

    // Should have networks array (may be empty or have results)
    expect(json).toHaveProperty('networks')
    expect(Array.isArray(json.networks)).toBe(true)
  })

  it('scan results have expected fields when available', async () => {
    // Trigger scan
    await fetch(`${BASE}/wifiman/scan`)
    // Wait for scan to complete
    await new Promise(r => setTimeout(r, 3000))

    const res = await fetch(`${BASE}/wifiman/scan`)
    const json = await res.json()

    if (json.networks.length > 0) {
      const net = json.networks[0]
      expect(net).toHaveProperty('ssid')
      expect(net).toHaveProperty('rssi')
      expect(net).toHaveProperty('encrypted')
      expect(typeof net.rssi).toBe('number')
      expect(typeof net.encrypted).toBe('boolean')
    }
  })
})
