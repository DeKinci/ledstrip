import { MicroProtoClient, MemoryStorage, type PropertySchema } from '@microproto/client'
import WebSocket from 'ws'

export interface SuiteManifest {
  name: string
  firmware: string | null
  board?: string
  capabilities: string[]
  runner?: 'vitest' | 'pytest'  // Test runner (default: vitest)
  timeout?: number       // Total suite timeout (default 60s)
  testTimeout?: number   // Per-test timeout (default 10s)
}

export interface DeviceContext {
  ip: string
  wsUrl: string
  client: MicroProtoClient
  /** Make an HTTP request to the device */
  http(path: string, init?: RequestInit): Promise<Response>
  /** Wait for the client to be ready (schema + values received) */
  waitReady(timeoutMs?: number): Promise<void>
  /** Wait for a specific property to appear in schema */
  waitProperty(name: string, timeoutMs?: number): Promise<PropertySchema>
  /** Disconnect and clean up */
  destroy(): void
}

export function createDeviceContext(ip: string): DeviceContext {
  const wsUrl = `ws://${ip}:81`

  const client = new MicroProtoClient(wsUrl, {
    reconnect: false,
    debug: false,
    storage: new MemoryStorage(),
    webSocketFactory: (url: string) => new WebSocket(url) as any,
  })

  const ctx: DeviceContext = {
    ip,
    wsUrl,
    client,

    http(path: string, init?: RequestInit) {
      const url = `http://${ip}${path.startsWith('/') ? path : '/' + path}`
      return fetch(url, init)
    },

    waitReady(timeoutMs = 10000) {
      return new Promise<void>((resolve, reject) => {
        const timeout = setTimeout(() => reject(new Error('Timeout waiting for ready')), timeoutMs)
        client.on('ready', () => { clearTimeout(timeout); resolve() })
        client.on('error', (err: any) => {
          clearTimeout(timeout)
          reject(new Error(err.message || String(err)))
        })
      })
    },

    waitProperty(name: string, timeoutMs = 10000) {
      return new Promise<PropertySchema>((resolve, reject) => {
        // Check if already present
        const existing = client.getPropertySchema(name)
        if (existing) { resolve(existing); return }

        const timeout = setTimeout(() => reject(new Error(`Timeout waiting for property '${name}'`)), timeoutMs)
        client.on('schema', (prop: any) => {
          if (prop.name === name) { clearTimeout(timeout); resolve(prop) }
        })
      })
    },

    destroy() {
      client.disconnect()
    },
  }

  return ctx
}
