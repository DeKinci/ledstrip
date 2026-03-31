import type { DeviceContext } from './context.js'

/**
 * Get the DeviceContext in a test file.
 * Available after global setup connects to the device.
 */
export function useDevice(): DeviceContext {
  const ctx = (globalThis as any).__deviceContext as DeviceContext | undefined
  if (!ctx) throw new Error('DeviceContext not initialized — are you running via test-device?')
  return ctx
}
