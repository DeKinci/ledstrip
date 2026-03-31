import { describe, it, expect } from 'vitest'
import { getDeviceIP } from '@test/device.js'

const BASE = `http://${getDeviceIP()}`

describe('URL Decoding - Path', () => {
  it('decodes %20 in path param', async () => {
    const res = await fetch(`${BASE}/user/hello%20world`)
    expect(res.status).toBe(200)
    const body = await res.json()
    expect(body.id).toBe('hello world')
  })

  it('%2F in path becomes real / (changes routing)', async () => {
    // %2F decodes to / which creates a deeper path: /user/a/b
    // This won't match /user/{id} (expects single segment) — returns 404
    const res = await fetch(`${BASE}/user/a%2Fb`)
    expect(res.status).toBe(404)
  })

  it('decodes multiple encoded chars', async () => {
    const res = await fetch(`${BASE}/user/%48%65%6C%6C%6F`)
    const body = await res.json()
    expect(body.id).toBe('Hello')
  })

  it('handles mixed encoded and plain chars', async () => {
    const res = await fetch(`${BASE}/user/foo%20bar%21baz`)
    const body = await res.json()
    expect(body.id).toBe('foo bar!baz')
  })

  it('passes through already-decoded paths', async () => {
    const res = await fetch(`${BASE}/user/plain`)
    const body = await res.json()
    expect(body.id).toBe('plain')
  })
})

describe('URL Decoding - Query Params', () => {
  it('decodes %20 in query value', async () => {
    const res = await fetch(`${BASE}/query?a=hello%20world&b=test`)
    const body = await res.json()
    expect(body.a).toBe('hello world')
    expect(body.b).toBe('test')
  })

  it('decodes + as space in query value', async () => {
    const res = await fetch(`${BASE}/query?a=hello+world`)
    const body = await res.json()
    expect(body.a).toBe('hello world')
  })

  it('decodes special chars in query', async () => {
    const res = await fetch(`${BASE}/query?a=%26%3D%3F`)
    const body = await res.json()
    expect(body.a).toBe('&=?')
  })

  it('handles empty encoded value', async () => {
    const res = await fetch(`${BASE}/query?a=%00`)
    expect(res.status).toBe(200)
  })
})
