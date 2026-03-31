import { describe, it, expect } from 'vitest'
import WebSocket from 'ws'
import { OPCODES, encodeVarint } from '@microproto/client'

describe('Ping/Pong', () => {
  it('receives PONG for each PING', async () => {
    const ip = process.env.DEVICE_IP!
    const wsUrl = `ws://${ip}:81`
    const count = 10
    const latencies: number[] = []

    const ws = new WebSocket(wsUrl)
    ws.binaryType = 'arraybuffer'

    await new Promise<void>((resolve, reject) => {
      ws.on('open', resolve)
      ws.on('error', reject)
    })

    // Send minimal HELLO to establish session
    const hello = new Uint8Array([0x00, 1, 0x80, 0x20, 0x01, 0x00, 0x00])
    ws.send(hello)

    // Wait for handshake (HELLO response + SCHEMA + VALUES)
    let handshakeMessages = 0
    await new Promise<void>((resolve) => {
      ws.on('message', function onMsg() {
        handshakeMessages++
        if (handshakeMessages >= 3) {
          ws.removeListener('message', onMsg)
          resolve()
        }
      })
    })

    // Send PINGs and collect PONGs
    for (let i = 0; i < count; i++) {
      const payload = encodeVarint(i + 1)
      const pingMsg = new Uint8Array(1 + payload.length)
      pingMsg[0] = OPCODES.PING
      pingMsg.set(payload, 1)

      const start = Date.now()
      ws.send(pingMsg)

      // Wait for PONG (skip property broadcasts)
      const pong = await new Promise<boolean>((resolve, reject) => {
        const timeout = setTimeout(() => reject(new Error(`PONG ${i + 1} timeout`)), 2000)
        const onMsg = (data: ArrayBuffer | Buffer) => {
          const bytes = new Uint8Array(data instanceof ArrayBuffer ? data : data.buffer)
          const opcode = bytes[0] & 0x0f
          const flags = (bytes[0] >> 4) & 0x0f

          // Skip property updates
          if (opcode === OPCODES.PROPERTY_UPDATE) return

          if (opcode === OPCODES.PING && (flags & 0x01)) {
            // PONG
            clearTimeout(timeout)
            ws.removeListener('message', onMsg)
            resolve(true)
          }
        }
        ws.on('message', onMsg)
      })

      const elapsed = Date.now() - start
      latencies.push(elapsed)
      console.log(`    [${i + 1}/${count}] PONG RTT=${elapsed}ms`)

      await new Promise(r => setTimeout(r, 100))
    }

    ws.close()

    expect(latencies.length).toBe(count)

    const avg = latencies.reduce((a, b) => a + b, 0) / latencies.length
    const max = Math.max(...latencies)
    console.log(`    RTT: avg=${avg.toFixed(0)}ms, max=${max}ms`)
    expect(avg).toBeLessThan(200) // Should be well under 200ms
  }, 30000)
})
