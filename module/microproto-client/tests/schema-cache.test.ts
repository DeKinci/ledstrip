import { describe, it, expect } from 'vitest'
import { SchemaCache, MemoryStorage, hashUrl } from '@microproto/client'
import type { PropertySchema } from '@microproto/client'

function makeProp(id: number, name: string): PropertySchema {
  return {
    id, name, typeId: 0x03, value: null, readonly: false, persistent: false, hidden: false,
    level: 0, groupId: 0, namespaceId: 0, bleExposed: false, description: null,
    constraints: {}, ui: { color: null, colorHex: null, unit: null, icon: null, widget: 0 },
  }
}

describe('hashUrl', () => {
  it('returns 8-char hex string', () => {
    const hash = hashUrl('ws://test:81')
    expect(hash.length).toBe(8)
    expect(/^[0-9a-f]{8}$/.test(hash)).toBe(true)
  })

  it('returns different hashes for different URLs', () => {
    expect(hashUrl('ws://a:81')).not.toBe(hashUrl('ws://b:81'))
  })
})

describe('SchemaCache', () => {
  it('stores and loads schema version', () => {
    const storage = new MemoryStorage()
    const cache = new SchemaCache(storage, 'test')

    expect(cache.loadSchemaVersion()).toBe(0)
    cache.saveSchemaVersion(42)
    expect(cache.loadSchemaVersion()).toBe(42)
  })

  it('stores and loads schema', () => {
    const storage = new MemoryStorage()
    const cache = new SchemaCache(storage, 'test')

    const props = new Map<number, PropertySchema>()
    props.set(0, makeProp(0, 'brightness'))
    props.set(1, makeProp(1, 'speed'))

    cache.saveSchema(props)

    const loaded = cache.loadSchema()
    expect(loaded).not.toBeNull()
    expect(loaded!.length).toBe(2)
    expect(loaded![0][1].name).toBe('brightness')
    expect(loaded![1][1].name).toBe('speed')
  })

  it('returns null for empty cache', () => {
    const storage = new MemoryStorage()
    const cache = new SchemaCache(storage, 'test')
    expect(cache.loadSchema()).toBeNull()
  })

  it('handles corrupted data', () => {
    const storage = new MemoryStorage()
    storage.setItem('test', 'not json')
    const cache = new SchemaCache(storage, 'test')
    expect(cache.loadSchema()).toBeNull()
  })
})

describe('MemoryStorage', () => {
  it('implements storage interface', () => {
    const storage = new MemoryStorage()
    expect(storage.getItem('key')).toBeNull()
    storage.setItem('key', 'val')
    expect(storage.getItem('key')).toBe('val')
    storage.removeItem('key')
    expect(storage.getItem('key')).toBeNull()
  })
})
