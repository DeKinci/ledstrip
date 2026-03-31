import { describe, it, expect } from 'vitest'
import { getDeviceIP } from '@test/device.js'

const BASE = `http://${getDeviceIP()}`

describe('Portal Page', () => {
  it('GET /wifiman serves HTML page', async () => {
    const res = await fetch(`${BASE}/wifiman`)
    expect(res.status).toBe(200)
    const html = await res.text()
    expect(html).toContain('<!DOCTYPE html>')
    expect(html).toContain('WiFi')
  })

  it('portal page has scan and network UI', async () => {
    const res = await fetch(`${BASE}/wifiman`)
    const html = await res.text()
    expect(html).toContain('scan')
    expect(html).toContain('ssid')
  })
})
