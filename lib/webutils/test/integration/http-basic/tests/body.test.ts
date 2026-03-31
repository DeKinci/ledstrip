import { describe, it, expect } from 'vitest'
import { getDeviceIP } from '@test/device.js'

const BASE = `http://${getDeviceIP()}`

describe('Request Body', () => {
  it('echoes short text', async () => {
    const res = await fetch(`${BASE}/echo`, {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body: 'hello',
    })
    expect(await res.text()).toBe('hello')
  })

  it('echoes JSON body', async () => {
    const payload = JSON.stringify({ key: 'value', num: 42 })
    const res = await fetch(`${BASE}/echo`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: payload,
    })
    expect(await res.text()).toBe(payload)
  })

  it('echoes 1KB body', async () => {
    const body = 'A'.repeat(1024)
    const res = await fetch(`${BASE}/echo`, {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body,
    })
    expect(await res.text()).toBe(body)
  })

  it('echoes 2KB body', async () => {
    const body = 'B'.repeat(2048)
    const res = await fetch(`${BASE}/echo`, {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body,
    })
    const text = await res.text()
    expect(text.length).toBe(2048)
    expect(text).toBe(body)
  })

  it('echoes body with special characters', async () => {
    const body = '{"emoji":"🔥","newline":"a\\nb","tabs":"a\\tb"}'
    const res = await fetch(`${BASE}/echo`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body,
    })
    expect(await res.text()).toBe(body)
  })

  it('handles empty POST body', async () => {
    const res = await fetch(`${BASE}/echo`, { method: 'POST' })
    expect(res.status).toBe(200)
    expect(await res.text()).toBe('')
  })

  it('POST /inspect returns body metadata', async () => {
    const body = 'test-body-123'
    const res = await fetch(`${BASE}/inspect`, {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body,
    })
    const json = await res.json()
    expect(json.method).toBe('POST')
    expect(json.path).toBe('/inspect')
    expect(json.bodyLength).toBe(body.length)
    expect(json.body).toBe(body)
    expect(json.contentType).toBe('text/plain')
  })

  it('POST /json-echo parses and echoes JSON', async () => {
    const res = await fetch(`${BASE}/json-echo`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ hello: 'world' }),
    })
    expect(res.status).toBe(200)
    const json = await res.json()
    expect(json.hello).toBe('world')
    expect(json.echoed).toBe(true)
  })

  it('POST /json-echo rejects invalid JSON', async () => {
    const res = await fetch(`${BASE}/json-echo`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: 'not json{{{',
    })
    expect(res.status).toBe(400)
  })
})
