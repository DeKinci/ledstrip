import { describe, it, expect } from 'vitest'
import { getDeviceIP } from '@test/device.js'

const BASE = `http://${getDeviceIP()}`

describe('Path Parameters', () => {
  it('single param', async () => {
    const res = await fetch(`${BASE}/user/42`)
    const json = await res.json()
    expect(json.id).toBe('42')
  })

  it('string param', async () => {
    const res = await fetch(`${BASE}/user/alice`)
    const json = await res.json()
    expect(json.id).toBe('alice')
  })

  it('nested path params', async () => {
    const res = await fetch(`${BASE}/api/v1/user/7/item/99`)
    expect(res.status).toBe(200)
    const json = await res.json()
    expect(json.userId).toBe('7')
    expect(json.itemId).toBe('99')
  })

  it('param with special chars', async () => {
    const res = await fetch(`${BASE}/user/AA:BB:CC`)
    const json = await res.json()
    expect(json.id).toBe('AA:BB:CC')
  })
})

describe('Query Parameters', () => {
  it('parses multiple params', async () => {
    const res = await fetch(`${BASE}/query?a=hello&b=world`)
    const json = await res.json()
    expect(json.a).toBe('hello')
    expect(json.b).toBe('world')
  })

  it('single param', async () => {
    const res = await fetch(`${BASE}/query?a=only`)
    const json = await res.json()
    expect(json.a).toBe('only')
    expect(json.b).toBeUndefined()
  })

  it('no params', async () => {
    const res = await fetch(`${BASE}/query`)
    expect(res.status).toBe(200)
  })

  it('empty param value', async () => {
    const res = await fetch(`${BASE}/query?a=&b=x`)
    const json = await res.json()
    expect(json.b).toBe('x')
  })
})

describe('404 Handling', () => {
  it('unknown path returns 404', async () => {
    const res = await fetch(`${BASE}/does/not/exist`)
    expect(res.status).toBe(404)
  })

  it('partial path match returns 404', async () => {
    const res = await fetch(`${BASE}/pin`)
    expect(res.status).toBe(404)
  })

  it('trailing slash mismatch', async () => {
    const res = await fetch(`${BASE}/ping/`)
    // Depends on implementation — either 404 or 200
    expect([200, 404]).toContain(res.status)
  })

  it('case sensitivity', async () => {
    const res = await fetch(`${BASE}/PING`)
    // HTTP paths are case-sensitive
    expect(res.status).toBe(404)
  })
})
