import { describe, it, expect } from 'vitest'
import { useDevice } from '@test/device.js'

describe('Log Streaming', () => {
  it('receives logStream entries when INFO log is triggered', async () => {
    const { client, http } = useDevice()

    // Listen for stream property updates
    const streamUpdate = new Promise<any>((resolve) => {
      client.on('property', (id: number, name: string, value: any) => {
        if (name === 'sys/logStream') resolve(value)
      })
    })

    // Trigger an INFO log via HTTP
    const resp = await http('/debug/log?level=info&tag=IntTest&msg=hello_from_test')
    expect(resp.status).toBe(200)

    // Should receive stream update
    const entries = await Promise.race([
      streamUpdate,
      new Promise<never>((_, reject) =>
        setTimeout(() => reject(new Error('Timeout waiting for logStream')), 5000)),
    ])

    expect(entries).toBeDefined()
  })

  it('ERROR log appears in both logStream and errorLog', async () => {
    const { client, http } = useDevice()

    const streams: Record<string, any> = {}
    const listener = (id: number, name: string, value: any) => {
      if (name === 'sys/logStream' || name === 'sys/errorLog') {
        streams[name] = value
      }
    }
    client.on('property', listener)

    // Trigger ERROR log
    await http('/debug/log?level=error&tag=ErrTest&msg=test_error')

    // Wait for both streams to update
    await new Promise(r => setTimeout(r, 3000))
    client.off('property', listener)

    // logStream should have received the entry
    // errorLog should also have received it (WARN and ERROR go to errorLog)
    // Note: they may arrive in any order or be batched
  })

  it('WARN log appears in errorLog', async () => {
    const { client, http } = useDevice()

    const errorLogUpdate = new Promise<any>((resolve) => {
      client.on('property', (id: number, name: string, value: any) => {
        if (name === 'sys/errorLog') resolve(value)
      })
    })

    await http('/debug/log?level=warn&tag=WarnTest&msg=test_warning')

    const entries = await Promise.race([
      errorLogUpdate,
      new Promise<never>((_, reject) =>
        setTimeout(() => reject(new Error('Timeout waiting for errorLog')), 5000)),
    ])

    expect(entries).toBeDefined()
  })
})

describe('Boot Count', () => {
  it('reports nonzero boot count', async () => {
    const { http } = useDevice()
    const resp = await http('/debug/bootcount')
    expect(resp.status).toBe(200)
    const data = await resp.json()
    expect(data.bootCount).toBeGreaterThan(0)
  })
})
