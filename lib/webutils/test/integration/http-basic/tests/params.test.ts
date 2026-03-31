import { describe, it, expect } from 'vitest'

import { getDeviceIP } from '@test/device.js'

const BASE = `http://${getDeviceIP()}`

describe('Query Parameters', () => {
  it('parses query params', async () => {
    const res = await fetch(`${BASE}/query?a=hello&b=world`)
    expect(res.status).toBe(200)
    const body = await res.json()
    expect(body.a).toBe('hello')
    expect(body.b).toBe('world')
  })

  it('handles missing params', async () => {
    const res = await fetch(`${BASE}/query?a=only`)
    const body = await res.json()
    expect(body.a).toBe('only')
    expect(body.b).toBeUndefined()
  })

  it('handles no params', async () => {
    const res = await fetch(`${BASE}/query`)
    expect(res.status).toBe(200)
  })
})

describe('Path Parameters', () => {
  it('extracts path param', async () => {
    const res = await fetch(`${BASE}/user/42`)
    expect(res.status).toBe(200)
    const body = await res.json()
    expect(body.id).toBe('42')
  })

  it('extracts string path param', async () => {
    const res = await fetch(`${BASE}/user/alice`)
    const body = await res.json()
    expect(body.id).toBe('alice')
  })
})
