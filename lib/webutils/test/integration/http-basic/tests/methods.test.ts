import { describe, it, expect } from 'vitest'
import { getDeviceIP } from '@test/device.js'

const BASE = `http://${getDeviceIP()}`

describe('HTTP Methods', () => {
  it('GET /ping', async () => {
    const res = await fetch(`${BASE}/ping`)
    expect(res.status).toBe(200)
  })

  it('POST /echo', async () => {
    const res = await fetch(`${BASE}/echo`, { method: 'POST', body: 'x' })
    expect(res.status).toBe(200)
    expect(await res.text()).toBe('x')
  })

  it('PUT /method', async () => {
    const res = await fetch(`${BASE}/method`, { method: 'PUT' })
    expect(res.status).toBe(200)
    expect(await res.text()).toBe('put-ok')
  })

  it('DELETE /method', async () => {
    const res = await fetch(`${BASE}/method`, { method: 'DELETE' })
    expect(res.status).toBe(200)
    expect(await res.text()).toBe('delete-ok')
  })

  it('wrong method returns 404', async () => {
    const res = await fetch(`${BASE}/ping`, { method: 'POST' })
    expect(res.status).toBe(404)
  })

  it('PUT to GET-only route returns 404', async () => {
    const res = await fetch(`${BASE}/ping`, { method: 'PUT' })
    expect(res.status).toBe(404)
  })
})
