// Matter debug API tests — verify test firmware is running correctly

import { describe, it, expect, beforeAll } from 'vitest'
import { getDeviceIP } from '@test/device.js'

const BASE = `http://${getDeviceIP()}`

describe('Debug API', () => {
  beforeAll(async () => {
    // Reset device state before tests
    await fetch(`${BASE}/debug/reset`, { method: 'POST' })
  })

  it('GET /debug/state returns cluster + session state', async () => {
    const res = await fetch(`${BASE}/debug/state`)
    expect(res.status).toBe(200)
    const json = await res.json()

    // Cluster state
    expect(json).toHaveProperty('onOff')
    expect(json).toHaveProperty('brightness')

    // Session state (per Matter spec state machine)
    expect(json).toHaveProperty('sessionState')
    expect(json).toHaveProperty('sessionSecure')
    expect(json).toHaveProperty('commissioned')
    expect(json).toHaveProperty('localSessionId')
    expect(json).toHaveProperty('peerSessionId')
  })

  it('initial cluster state after reset', async () => {
    const json = await (await fetch(`${BASE}/debug/state`)).json()
    expect(json.onOff).toBe(false)
    expect(json.brightness).toBe(128)
  })

  it('initial session state is PASE_WAIT_PBKDF_REQ', async () => {
    // After begin(), device should be waiting for commissioning
    const json = await (await fetch(`${BASE}/debug/state`)).json()
    expect(json.sessionState).toBe('PASE_WAIT_PBKDF_REQ')
    expect(json.sessionSecure).toBe(false)
    expect(json.commissioned).toBe(false)
  })

  it('GET /debug/session returns detailed session info', async () => {
    const json = await (await fetch(`${BASE}/debug/session`)).json()
    expect(json).toHaveProperty('state')
    expect(json).toHaveProperty('secure')
    expect(json).toHaveProperty('commissioned')
    expect(json).toHaveProperty('localSessionId')
    expect(json).toHaveProperty('peerSessionId')
    expect(json).toHaveProperty('nodeId')
  })

  it('POST /debug/reset resets cluster state', async () => {
    const res = await fetch(`${BASE}/debug/reset`, { method: 'POST' })
    expect(res.status).toBe(200)

    const state = await (await fetch(`${BASE}/debug/state`)).json()
    expect(state.onOff).toBe(false)
    expect(state.brightness).toBe(128)
  })

  it('POST /debug/reset-session re-opens commissioning', async () => {
    const res = await fetch(`${BASE}/debug/reset-session`, { method: 'POST' })
    expect(res.status).toBe(200)

    const state = await (await fetch(`${BASE}/debug/state`)).json()
    expect(state.sessionState).toBe('PASE_WAIT_PBKDF_REQ')
    expect(state.sessionSecure).toBe(false)
  })
})
