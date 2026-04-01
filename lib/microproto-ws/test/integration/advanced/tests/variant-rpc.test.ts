import { describe, it, expect } from 'vitest'
import { useDevice } from '@test/device.js'
import { TYPES } from '@microproto/client'

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

describe('Variant Properties', () => {
  it('schema has correct variant types', () => {
    const { client } = useDevice()
    const variant = client.getPropertySchema('test/variant')!
    expect(variant.typeId).toBe(TYPES.VARIANT)
    expect(variant.variants).toBeDefined()
    expect(variant.variants!.length).toBe(3)
    expect(variant.variants![0].name).toBe('ok')
    expect(variant.variants![1].name).toBe('error')
    expect(variant.variants![2].name).toBe('flag')
  })

  it('receives initial variant value', () => {
    const { client } = useDevice()
    const val = client.getProperty('test/variant')
    expect(val).toBeDefined()
    expect(val._index).toBe(0)  // "ok" type
    expect(val.value).toBe(42)  // uint8 = 42
  })

  it('syncs variant type switch to error', async () => {
    const { client } = useDevice()
    const echo = waitEcho(client, 'test/variant')
    client.setProperty('test/variant', { _index: 1, value: -999 })
    const val = await echo
    expect(val._index).toBe(1)
    expect(val.value).toBe(-999)
  })

  it('syncs variant type switch to flag', async () => {
    const { client } = useDevice()
    const echo = waitEcho(client, 'test/variant')
    client.setProperty('test/variant', { _index: 2, value: true })
    const val = await echo
    expect(val._index).toBe(2)
    expect(val.value).toBe(true)
  })

  it('syncs variant back to ok', async () => {
    const { client } = useDevice()
    const echo = waitEcho(client, 'test/variant')
    client.setProperty('test/variant', { _index: 0, value: 100 })
    const val = await echo
    expect(val._index).toBe(0)
    expect(val.value).toBe(100)
  })
})

describe('RPC Functions', () => {
  it('schema has registered functions', () => {
    const { client } = useDevice()
    expect(client.functionByName.has('test/add')).toBe(true)
    expect(client.functionByName.has('test/emitEvent')).toBe(true)
    expect(client.functionByName.has('test/noParams')).toBe(true)
  })

  it('function schema has param definitions', () => {
    const { client } = useDevice()
    const addId = client.functionByName.get('test/add')!
    const addFn = client.functions.get(addId)!
    expect(addFn.params.length).toBe(2)
    expect(addFn.params[0].name).toBe('a')
    expect(addFn.params[0].typeId).toBe(TYPES.INT32)
    expect(addFn.params[1].name).toBe('b')
    expect(addFn.returnTypeId).toBe(TYPES.INT32)
  })

  it('calls add function with correct result', async () => {
    const { client } = useDevice()

    // Encode params: two int32 LE
    const params = new ArrayBuffer(8)
    const view = new DataView(params)
    view.setInt32(0, 100, true)
    view.setInt32(4, 200, true)

    const result = await client.callFunction('test/add', new Uint8Array(params))
    expect(result.success).toBe(true)
  })

  it('calls no-param function', async () => {
    const { client } = useDevice()
    const result = await client.callFunction('test/noParams')
    expect(result.success).toBe(true)
  })

  it('emitEvent triggers stream entry', async () => {
    const { client } = useDevice()

    // Listen for stream update
    const streamUpdate = new Promise<any>((resolve) => {
      client.on('property', (id: number, name: string, value: any) => {
        if (name === 'test/events') resolve(value)
      })
    })

    // Call emitEvent with code=42
    const params = new Uint8Array([42])
    const result = await client.callFunction('test/emitEvent', params)
    expect(result.success).toBe(true)

    // Should receive stream update with the event
    const events = await Promise.race([
      streamUpdate,
      new Promise<never>((_, reject) =>
        setTimeout(() => reject(new Error('Timeout waiting for stream event')), 5000)),
    ])

    expect(events).toBeDefined()
  })
})
