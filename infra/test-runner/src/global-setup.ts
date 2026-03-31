import fs from 'fs'
import path from 'path'
import { createDeviceContext } from './context.js'
import { discoverDevice } from './discovery.js'

let cleanup: (() => void) | null = null

// Vitest globalSetup runs in the main process.
// We write DEVICE_IP to a temp file so test workers can read it.
const IP_FILE = path.join(process.cwd(), 'node_modules', '.cache', 'device-ip.txt')

export async function setup() {
  // Check if integration tests are in this run
  const isIntegration = process.env.SUITE_PATH ||
    process.argv.some(a => a.includes('/integration/'))

  if (!isIntegration) return

  // Auto-discover device if DEVICE_IP not set
  let ip = process.env.DEVICE_IP
  if (!ip) {
    ip = await discoverDevice()
    if (!ip) throw new Error('No device found. Set DEVICE_IP or connect a device to the network.')
  }

  // Write IP for test workers to read
  fs.mkdirSync(path.dirname(IP_FILE), { recursive: true })
  fs.writeFileSync(IP_FILE, ip)
  process.env.DEVICE_IP = ip

  // Read suite manifest to check capabilities
  const suitePath = process.env.SUITE_PATH
  let capabilities: string[] = ['ws']

  if (suitePath) {
    const manifestPath = path.join(suitePath, 'suite.json')
    if (fs.existsSync(manifestPath)) {
      const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf-8'))
      capabilities = manifest.capabilities || ['ws']
    }
  }

  if (capabilities.includes('ws')) {
    const deviceCtx = createDeviceContext(ip)
    deviceCtx.client.connect()
    await deviceCtx.waitReady()
    ;(globalThis as any).__deviceContext = deviceCtx
    cleanup = () => deviceCtx.destroy()
  } else {
    const res = await fetch(`http://${ip}/ping`, { signal: AbortSignal.timeout(5000) })
      .catch(() => null)
    if (!res || !res.ok) {
      throw new Error(`Device at ${ip} not reachable via HTTP`)
    }
  }
}

export async function teardown() {
  cleanup?.()
  cleanup = null
  try { fs.unlinkSync(IP_FILE) } catch {}
}
