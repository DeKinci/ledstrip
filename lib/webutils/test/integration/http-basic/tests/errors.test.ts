import { describe, it, expect } from 'vitest'
import { getDeviceIP } from '@test/device.js'
import { connect } from 'net'

const BASE = `http://${getDeviceIP()}`
const IP = getDeviceIP()

describe('413 Payload Too Large', () => {
  it('rejects body > 8KB', async () => {
    const body = 'X'.repeat(16384) // 16KB, well over 8KB limit
    const res = await fetch(`${BASE}/echo`, {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body,
    })
    expect(res.status).toBe(413)
  })

  it('rejects 1MB body', async () => {
    const body = 'Y'.repeat(1024 * 1024)
    const res = await fetch(`${BASE}/echo`, {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body,
    })
    expect(res.status).toBe(413)
  })

  it('accepts body that fits in buffer (~3.5KB)', async () => {
    // Buffer is 4KB total (headers + body). Headers take ~200 bytes.
    const body = 'Z'.repeat(3500)
    const res = await fetch(`${BASE}/echo`, {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body,
    })
    expect(res.status).toBe(200)
    expect((await res.text()).length).toBe(3500)
  })

  it('rejects body that overflows buffer but under maxBodySize', async () => {
    // 5KB body: passes the 8KB maxBodySize check but overflows 4KB buffer → 400
    const body = 'A'.repeat(5000)
    const res = await fetch(`${BASE}/echo`, {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body,
    })
    expect([400, 413]).toContain(res.status)
  })
})

describe('Malformed Requests', () => {
  // Helper: send raw TCP data and read response
  function rawRequest(data: string): Promise<string> {
    return new Promise((resolve, reject) => {
      const sock = connect(80, IP, () => {
        sock.write(data)
      })
      let response = ''
      sock.on('data', (chunk) => { response += chunk.toString() })
      sock.on('end', () => resolve(response))
      sock.on('error', reject)
      setTimeout(() => { sock.destroy(); resolve(response) }, 3000)
    })
  }

  it('rejects incomplete request line', async () => {
    const res = await rawRequest('INVALID\r\n\r\n')
    expect(res).toContain('400')
  })

  it('rejects request with no headers terminator', async () => {
    // Send partial request, wait for timeout → 408 or 400
    const res = await rawRequest('GET /ping HTTP/1.1\r\n')
    expect(res).toMatch(/40[08]/)
  })

  it('rejects request with garbage data', async () => {
    const res = await rawRequest('\x00\x01\x02\x03\r\n\r\n')
    expect(res).toContain('400')
  })

  it('handles connection close without data', { timeout: 15000 }, async () => {
    // Connect and immediately close — should not crash the server
    await new Promise<void>((resolve) => {
      const sock = connect(80, IP, () => {
        sock.destroy()
        resolve()
      })
      sock.on('error', () => resolve())
    })
    // Wait for server to release the dead connection (firstByteTimeout)
    await new Promise(r => setTimeout(r, 6000))
    // Verify server still works
    const res = await fetch(`${BASE}/ping`)
    expect(res.status).toBe(200)
  })

  it('handles partial body (Content-Length mismatch)', async () => {
    // Claim 1000 bytes but only send 10
    const res = await rawRequest(
      'POST /echo HTTP/1.1\r\n' +
      'Content-Length: 1000\r\n' +
      '\r\n' +
      'short body'
    )
    // Should timeout waiting for remaining body → 400
    expect(res).toMatch(/400/)
  })
})

describe('Recovery After Errors', () => {
  it('server works after 413 rejection', async () => {
    // Trigger 413
    await fetch(`${BASE}/echo`, {
      method: 'POST',
      body: 'X'.repeat(16384),
    }).catch(() => {})

    // Should still work
    const res = await fetch(`${BASE}/ping`)
    expect(res.status).toBe(200)
    expect(await res.text()).toBe('pong')
  })

  it('server works after malformed request', async () => {
    // Send garbage
    await new Promise<void>((resolve) => {
      const sock = connect(80, IP, () => {
        sock.write('\x00\x01\r\n\r\n')
        setTimeout(() => { sock.destroy(); resolve() }, 500)
      })
      sock.on('error', () => resolve())
    })

    // Should still work
    const res = await fetch(`${BASE}/ping`)
    expect(res.status).toBe(200)
  })

  it('server works after 10 sequential error requests', async () => {
    for (let i = 0; i < 10; i++) {
      await fetch(`${BASE}/echo`, {
        method: 'POST',
        body: 'X'.repeat(16384),
      }).catch(() => {})
    }

    const res = await fetch(`${BASE}/ping`)
    expect(res.status).toBe(200)
    expect(await res.text()).toBe('pong')
  })

  it('server works after mixed valid and invalid requests', async () => {
    for (let i = 0; i < 20; i++) {
      if (i % 3 === 0) {
        // Invalid
        await fetch(`${BASE}/echo`, {
          method: 'POST',
          body: 'X'.repeat(16384),
        }).catch(() => {})
      } else {
        // Valid
        const res = await fetch(`${BASE}/ping`)
        expect(res.status).toBe(200)
      }
    }
    // Final check
    const res = await fetch(`${BASE}/json`)
    expect(res.status).toBe(200)
    const body = await res.json()
    expect(body.status).toBe('ok')
  })
})
