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

describe('Array Properties', () => {
  it('schema has correct type and element info', () => {
    const { client } = useDevice()

    const rgb = client.getPropertySchema('test/rgb')!
    expect(rgb.typeId).toBe(TYPES.ARRAY)
    expect(rgb.elementTypeId).toBe(TYPES.UINT8)
    expect(rgb.elementCount).toBe(3)

    const intArr = client.getPropertySchema('test/intArray')!
    expect(intArr.typeId).toBe(TYPES.ARRAY)
    expect(intArr.elementTypeId).toBe(TYPES.INT32)
    expect(intArr.elementCount).toBe(4)
  })

  it('receives initial array values', () => {
    const { client } = useDevice()
    const rgb = client.getProperty('test/rgb')
    expect(rgb).toEqual([255, 128, 0])

    const intArr = client.getProperty('test/intArray')
    expect(intArr).toEqual([10, -20, 30, -40])
  })

  it('syncs RGB array update', async () => {
    const { client } = useDevice()
    const echo = waitEcho(client, 'test/rgb')
    client.setProperty('test/rgb', [0, 255, 128])
    expect(await echo).toEqual([0, 255, 128])
  })

  it('syncs INT32 array with negatives', async () => {
    const { client } = useDevice()
    const echo = waitEcho(client, 'test/intArray')
    client.setProperty('test/intArray', [-1000, 0, 500, -999])
    expect(await echo).toEqual([-1000, 0, 500, -999])
  })

  it('array has element constraints', () => {
    const { client } = useDevice()
    const rgb = client.getPropertySchema('test/rgb')!
    expect(rgb.elementConstraints).toBeDefined()
    expect(rgb.elementConstraints!.hasMin).toBe(true)
    expect(rgb.elementConstraints!.min).toBe(0)
    expect(rgb.elementConstraints!.max).toBe(255)
  })
})

describe('List Properties', () => {
  it('schema has correct type info', () => {
    const { client } = useDevice()

    const byteList = client.getPropertySchema('test/byteList')!
    expect(byteList.typeId).toBe(TYPES.LIST)
    expect(byteList.elementTypeId).toBe(TYPES.UINT8)

    const intList = client.getPropertySchema('test/intList')!
    expect(intList.typeId).toBe(TYPES.LIST)
    expect(intList.elementTypeId).toBe(TYPES.INT32)
  })

  it('receives initial list values', () => {
    const { client } = useDevice()
    const byteList = client.getProperty('test/byteList')
    expect(byteList).toEqual([0x48, 0x65, 0x6C, 0x6C, 0x6F])

    const intList = client.getProperty('test/intList')
    expect(intList).toEqual([100, -200, 300])
  })

  it('syncs byte list update', async () => {
    const { client } = useDevice()
    const echo = waitEcho(client, 'test/byteList')
    client.setProperty('test/byteList', [1, 2, 3, 4, 5, 6, 7, 8])
    expect(await echo).toEqual([1, 2, 3, 4, 5, 6, 7, 8])
  })

  it('syncs empty list', async () => {
    const { client } = useDevice()
    const echo = waitEcho(client, 'test/intList')
    client.setProperty('test/intList', [])
    expect(await echo).toEqual([])
  })

  it('syncs single-element list', async () => {
    const { client } = useDevice()
    const echo = waitEcho(client, 'test/intList')
    client.setProperty('test/intList', [42])
    expect(await echo).toEqual([42])
  })

  it('syncs list with negative values', async () => {
    const { client } = useDevice()
    const echo = waitEcho(client, 'test/intList')
    client.setProperty('test/intList', [-1, -2, -3, -4, -5])
    expect(await echo).toEqual([-1, -2, -3, -4, -5])
  })
})

describe('Object Properties', () => {
  it('schema has correct type and fields', () => {
    const { client } = useDevice()
    const point = client.getPropertySchema('test/point')!
    expect(point.typeId).toBe(TYPES.OBJECT)
    expect(point.fields).toBeDefined()
    expect(point.fields!.length).toBe(3)
    expect(point.fields![0].name).toBe('x')
    expect(point.fields![1].name).toBe('y')
    expect(point.fields![2].name).toBe('z')
  })

  it('receives initial object value', () => {
    const { client } = useDevice()
    const point = client.getProperty('test/point')
    expect(point.x).toBe(10)
    expect(point.y).toBe(-20)
    expect(point.z).toBe(30)
  })

  it('syncs object update', async () => {
    const { client } = useDevice()
    const echo = waitEcho(client, 'test/point')
    client.setProperty('test/point', { x: -100, y: 200, z: -300 })
    const val = await echo
    expect(val.x).toBe(-100)
    expect(val.y).toBe(200)
    expect(val.z).toBe(-300)
  })

  it('syncs object with zero values', async () => {
    const { client } = useDevice()
    const echo = waitEcho(client, 'test/point')
    client.setProperty('test/point', { x: 0, y: 0, z: 0 })
    const val = await echo
    expect(val.x).toBe(0)
    expect(val.y).toBe(0)
    expect(val.z).toBe(0)
  })
})

describe('List of Objects', () => {
  it('schema has OBJECT element type', () => {
    const { client } = useDevice()
    const segments = client.getPropertySchema('test/segments')!
    expect(segments.typeId).toBe(TYPES.LIST)
    expect(segments.elementTypeId).toBe(TYPES.OBJECT)
    expect(segments.elementTypeDef).toBeDefined()
  })

  it('receives initial segments', () => {
    const { client } = useDevice()
    const segments = client.getProperty('test/segments')
    expect(segments.length).toBe(2)
    expect(segments[0].start).toBe(0)
    expect(segments[0].length).toBe(10)
    expect(segments[1].start).toBe(10)
    expect(segments[1].length).toBe(20)
  })

  it('syncs segment list update', async () => {
    const { client } = useDevice()
    const echo = waitEcho(client, 'test/segments')
    client.setProperty('test/segments', [
      { start: 0, length: 5, flags: 3, _pad1: 0, _pad2: 0, _pad3: 0 },
    ])
    const val = await echo
    expect(val.length).toBe(1)
    expect(val[0].start).toBe(0)
    expect(val[0].length).toBe(5)
    expect(val[0].flags).toBe(3)
  })
})
