import { describe, it, expect } from 'vitest'
import { getDeviceIP } from '@test/device.js'

const BASE = `http://${getDeviceIP()}`

describe('Network CRUD', () => {
  it('GET /wifiman/list returns networks array', async () => {
    const res = await fetch(`${BASE}/wifiman/list`)
    expect(res.status).toBe(200)
    const json = await res.json()
    expect(json).toHaveProperty('networks')
    expect(Array.isArray(json.networks)).toBe(true)
  })

  it('POST /wifiman/add adds a network', async () => {
    const res = await fetch(`${BASE}/wifiman/add`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ssid: 'TestNet', password: 'pass123', priority: 50 }),
    })
    expect(res.status).toBe(200)
    const json = await res.json()
    expect(json.success).toBe(true)

    // Verify it appears in the list
    const list = await (await fetch(`${BASE}/wifiman/list`)).json()
    const found = list.networks.find((n: any) => n.ssid === 'TestNet')
    expect(found).toBeTruthy()
    expect(found.priority).toBe(50)
  })

  it('POST /wifiman/add rejects missing ssid', async () => {
    const res = await fetch(`${BASE}/wifiman/add`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ password: 'no-ssid' }),
    })
    expect(res.status).toBe(400)
  })

  it('POST /wifiman/add rejects invalid JSON', async () => {
    const res = await fetch(`${BASE}/wifiman/add`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: 'not json',
    })
    expect(res.status).toBe(400)
  })

  it('POST /wifiman/remove removes a network', async () => {
    // Add first
    await fetch(`${BASE}/wifiman/add`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ssid: 'ToRemove', password: '' }),
    })

    // Remove
    const res = await fetch(`${BASE}/wifiman/remove`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ssid: 'ToRemove' }),
    })
    expect(res.status).toBe(200)

    // Verify gone
    const list = await (await fetch(`${BASE}/wifiman/list`)).json()
    const found = list.networks.find((n: any) => n.ssid === 'ToRemove')
    expect(found).toBeUndefined()
  })

  it('POST /wifiman/remove rejects missing ssid', async () => {
    const res = await fetch(`${BASE}/wifiman/remove`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({}),
    })
    expect(res.status).toBe(400)
  })

  it('POST /wifiman/clear removes all networks', async () => {
    // Add a couple
    await fetch(`${BASE}/wifiman/add`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ssid: 'Net1', password: '' }),
    })
    await fetch(`${BASE}/wifiman/add`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ssid: 'Net2', password: '' }),
    })

    // Clear
    const res = await fetch(`${BASE}/wifiman/clear`, { method: 'POST' })
    expect(res.status).toBe(200)

    // Verify empty (except default networks if any)
    const list = await (await fetch(`${BASE}/wifiman/list`)).json()
    const testNets = list.networks.filter((n: any) => n.ssid === 'Net1' || n.ssid === 'Net2')
    expect(testNets.length).toBe(0)
  })
})
