import { describe, it, expect } from 'vitest'
import { useDevice } from '@test/device.js'
import { TYPES } from '@microproto/client'

/** Wait for a property echo with timeout */
function waitEcho(client: any, name: string, timeout = 5000): Promise<any> {
  return Promise.race([
    new Promise<any>((resolve) => {
      client.on('property', (id: number, n: string, value: any) => {
        if (n === name) resolve(value)
      })
    }),
    new Promise<never>((_, reject) =>
      setTimeout(() => reject(new Error(`Timeout waiting for ${name} echo`)), timeout)),
  ])
}

describe('Basic Types — Schema', () => {
  it('has correct type IDs for all basic types', () => {
    const { client } = useDevice()
    const cases: [string, number][] = [
      ['test/bool', TYPES.BOOL],
      ['test/int8', TYPES.INT8],
      ['test/uint8', TYPES.UINT8],
      ['test/int16', TYPES.INT16],
      ['test/uint16', TYPES.UINT16],
      ['test/int32', TYPES.INT32],
      ['test/float', TYPES.FLOAT32],
    ]
    for (const [name, expectedType] of cases) {
      const schema = client.getPropertySchema(name)
      expect(schema, `${name} should exist in schema`).toBeDefined()
      expect(schema!.typeId).toBe(expectedType)
    }
  })

  it('has constraints on constrained properties', () => {
    const { client } = useDevice()

    const uint8 = client.getPropertySchema('test/uint8')!
    expect(uint8.constraints.hasMin).toBe(true)
    expect(uint8.constraints.min).toBe(0)
    expect(uint8.constraints.hasMax).toBe(true)
    expect(uint8.constraints.max).toBe(255)
    expect(uint8.constraints.hasStep).toBe(true)
    expect(uint8.constraints.step).toBe(1)

    const int8 = client.getPropertySchema('test/int8')!
    expect(int8.constraints.min).toBe(-100)
    expect(int8.constraints.max).toBe(100)

    const float32 = client.getPropertySchema('test/float')!
    expect(float32.constraints.min).toBeCloseTo(-100.0)
    expect(float32.constraints.max).toBeCloseTo(100.0)
    expect(float32.constraints.step).toBeCloseTo(0.01)
  })

  it('has UI hints', () => {
    const { client } = useDevice()
    const uint8 = client.getPropertySchema('test/uint8')!
    expect(uint8.ui.color).toBe('amber')
    expect(uint8.ui.unit).toBe('%')
  })
})

describe('Basic Types — Initial Values', () => {
  it('receives correct defaults', () => {
    const { client } = useDevice()
    expect(client.getProperty('test/bool')).toBe(true)
    expect(client.getProperty('test/int8')).toBe(-42)
    expect(client.getProperty('test/uint8')).toBe(128)
    expect(client.getProperty('test/int16')).toBe(-1000)
    expect(client.getProperty('test/uint16')).toBe(5000)
    expect(client.getProperty('test/int32')).toBe(-100000)
    expect(client.getProperty('test/float')).toBeCloseTo(3.14, 1)
  })
})

describe('Basic Types — Sync', () => {
  it('syncs BOOL toggle', async () => {
    const { client } = useDevice()
    const current = client.getProperty('test/bool')
    const echo = waitEcho(client, 'test/bool')
    client.setProperty('test/bool', !current)
    expect(await echo).toBe(!current)
  })

  it('syncs INT8 negative value', async () => {
    const { client } = useDevice()
    const echo = waitEcho(client, 'test/int8')
    client.setProperty('test/int8', -99)
    expect(await echo).toBe(-99)
  })

  it('syncs UINT8', async () => {
    const { client } = useDevice()
    const echo = waitEcho(client, 'test/uint8')
    client.setProperty('test/uint8', 200)
    expect(await echo).toBe(200)
  })

  it('syncs INT16 negative', async () => {
    const { client } = useDevice()
    const echo = waitEcho(client, 'test/int16')
    client.setProperty('test/int16', -30000)
    expect(await echo).toBe(-30000)
  })

  it('syncs UINT16', async () => {
    const { client } = useDevice()
    const echo = waitEcho(client, 'test/uint16')
    client.setProperty('test/uint16', 60000)
    expect(await echo).toBe(60000)
  })

  it('syncs INT32 large negative', async () => {
    const { client } = useDevice()
    const echo = waitEcho(client, 'test/int32')
    client.setProperty('test/int32', -999999)
    expect(await echo).toBe(-999999)
  })

  it('syncs FLOAT32', async () => {
    const { client } = useDevice()
    const echo = waitEcho(client, 'test/float')
    client.setProperty('test/float', -42.5)
    const val = await echo
    expect(Math.abs(val - (-42.5))).toBeLessThan(0.01)
  })
})

describe('Basic Types — Edge Cases', () => {
  it('syncs zero values', async () => {
    const { client } = useDevice()
    for (const name of ['test/int8', 'test/uint8', 'test/int16', 'test/uint16', 'test/int32', 'test/float']) {
      const echo = waitEcho(client, name)
      client.setProperty(name, 0)
      expect(await echo).toBe(0)
    }
  })

  it('syncs BOOL false', async () => {
    const { client } = useDevice()
    const echo = waitEcho(client, 'test/bool')
    client.setProperty('test/bool', false)
    expect(await echo).toBe(false)
  })

  it('syncs FLOAT32 near-zero', async () => {
    const { client } = useDevice()
    const echo = waitEcho(client, 'test/float')
    client.setProperty('test/float', 0.001)
    const val = await echo
    expect(Math.abs(val - 0.001)).toBeLessThan(0.001)
  })

  it('syncs INT32 boundary values', async () => {
    const { client } = useDevice()
    // Within constraint range [-1000000, 1000000]
    const echo = waitEcho(client, 'test/int32')
    client.setProperty('test/int32', 1000000)
    expect(await echo).toBe(1000000)
  })
})
