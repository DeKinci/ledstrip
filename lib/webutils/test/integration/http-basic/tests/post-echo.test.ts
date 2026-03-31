import { describe, it, expect } from 'vitest'

import { getDeviceIP } from '@test/device.js'

const BASE = `http://${getDeviceIP()}`

describe('POST Echo', () => {
  it('echoes plain text body', async () => {
    const res = await fetch(`${BASE}/echo`, {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body: 'hello world',
    })
    expect(res.status).toBe(200)
    expect(await res.text()).toBe('hello world')
  })

  it('echoes JSON body', async () => {
    const payload = JSON.stringify({ name: 'test', value: 42 })
    const res = await fetch(`${BASE}/echo`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: payload,
    })
    expect(res.status).toBe(200)
    expect(await res.text()).toBe(payload)
  })

  it('handles empty body', async () => {
    const res = await fetch(`${BASE}/echo`, { method: 'POST' })
    expect(res.status).toBe(200)
    expect(await res.text()).toBe('')
  })
})
