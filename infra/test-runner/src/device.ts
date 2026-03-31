import fs from 'fs'
import path from 'path'
import type { DeviceContext } from './context.js'

const IP_FILE = path.join(process.cwd(), 'node_modules', '.cache', 'device-ip.txt')

/** Get the device IP. Reads from env or cache file written by globalSetup. */
export function getDeviceIP(): string {
  const ip = process.env.DEVICE_IP
  if (ip) return ip

  try {
    const cached = fs.readFileSync(IP_FILE, 'utf-8').trim()
    if (cached) {
      process.env.DEVICE_IP = cached
      return cached
    }
  } catch {}

  throw new Error('No device IP. Run via: npm run test:device -- <suite>')
}

/** Get the DeviceContext (WebSocket-connected client). For MicroProto suites. */
export function useDevice(): DeviceContext {
  const ctx = (globalThis as any).__deviceContext as DeviceContext | undefined
  if (!ctx) throw new Error('DeviceContext not initialized — are you running via test-device?')
  return ctx
}
