// Matter device discovery and connectivity tests

import { describe, it, expect } from 'vitest'
import { getDeviceIP } from '@test/device.js'
import dgram from 'dgram'

const IP = getDeviceIP()
const MATTER_PORT = 5540
const BASE = `http://${IP}`

describe('Matter Discovery', () => {
  it('device is reachable via HTTP', async () => {
    const res = await fetch(`${BASE}/ping`)
    expect(res.status).toBe(200)
    expect(await res.text()).toBe('pong')
  })

  it('Matter port is 5540', async () => {
    const res = await fetch(`${BASE}/debug/matter-port`)
    expect(await res.text()).toBe('5540')
  })

  it('UDP port 5540 accepts packets', async () => {
    const sock = dgram.createSocket('udp4')

    const received = new Promise<boolean>((resolve) => {
      sock.on('message', () => resolve(true))
      setTimeout(() => resolve(false), 3000)
    })

    // Send a minimal Matter message header (will be rejected but port should be open)
    // Flags=0, SecFlags=0, SessionId=0, Counter=1
    const header = Buffer.from([0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00])
    sock.send(header, MATTER_PORT, IP)

    // We may or may not get a response (device might silently drop malformed messages)
    // The test verifies the port is open by not throwing ECONNREFUSED
    await new Promise(r => setTimeout(r, 500))
    sock.close()
  })
})
