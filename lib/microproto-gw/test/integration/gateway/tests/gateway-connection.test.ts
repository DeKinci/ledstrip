import { describe, it, expect } from 'vitest'

/**
 * Gateway connection tests.
 *
 * These tests verify the device's gateway client behavior by:
 * 1. Checking HTTP debug endpoint for gateway connection status
 * 2. Verifying the device reports its gateway state correctly
 *
 * Note: Full gateway integration requires a running gateway server.
 * When GATEWAY_URL is not configured, the gateway client stays disabled.
 * These tests verify the graceful handling of that case.
 */
describe('Gateway Client', () => {
  const ip = process.env.DEVICE_IP!
  const baseUrl = `http://${ip}`

  it('device is reachable via HTTP', async () => {
    const resp = await fetch(`${baseUrl}/ping`)
    expect(resp.status).toBe(200)
    const text = await resp.text()
    expect(text).toBe('pong')
  })

  it('reports gateway status via debug endpoint', async () => {
    const resp = await fetch(`${baseUrl}/debug/gateway`)
    expect(resp.status).toBe(200)
    const data = await resp.json()
    expect(data).toHaveProperty('connected')
    expect(typeof data.connected).toBe('boolean')
    expect(data).toHaveProperty('brightness')
    expect(data).toHaveProperty('enabled')
  })

  it('gateway disconnected when no server running', async () => {
    // With no gateway server, the client should report disconnected
    const resp = await fetch(`${baseUrl}/debug/gateway`)
    const data = await resp.json()
    expect(data.connected).toBe(false)
  })

  it('properties have correct initial values', async () => {
    const resp = await fetch(`${baseUrl}/debug/gateway`)
    const data = await resp.json()
    expect(data.brightness).toBe(128)
    expect(data.enabled).toBe(true)
  })
})
