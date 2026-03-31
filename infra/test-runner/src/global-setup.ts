import { createDeviceContext, type DeviceContext } from './context.js'

let ctx: DeviceContext | null = null

export async function setup() {
  const ip = process.env.DEVICE_IP
  if (!ip) throw new Error('DEVICE_IP not set')

  ctx = createDeviceContext(ip)
  ctx.client.connect()
  await ctx.waitReady()

  // Make context available to tests via env
  // Tests import useDevice() which reads from globalThis
  ;(globalThis as any).__deviceContext = ctx
}

export async function teardown() {
  ctx?.destroy()
  ctx = null
}
