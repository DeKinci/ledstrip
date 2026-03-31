import type { StorageLike, PropertySchema } from './types.js'

/** In-memory storage for environments without localStorage */
export class MemoryStorage implements StorageLike {
  private store = new Map<string, string>()
  getItem(key: string): string | null { return this.store.get(key) ?? null }
  setItem(key: string, value: string): void { this.store.set(key, value) }
  removeItem(key: string): void { this.store.delete(key) }
}

/** FNV-1a hash of a string → 8 hex chars */
export function hashUrl(url: string): string {
  let hash = 2166136261
  for (let i = 0; i < url.length; i++) {
    hash ^= url.charCodeAt(i)
    hash = (hash * 16777619) >>> 0
  }
  return hash.toString(16).padStart(8, '0')
}

export class SchemaCache {
  private storage: StorageLike
  private key: string

  constructor(storage: StorageLike, key: string) {
    this.storage = storage
    this.key = key
  }

  loadSchemaVersion(): number {
    try {
      return parseInt(this.storage.getItem(this.key + '_v') || '0', 10) || 0
    } catch { return 0 }
  }

  saveSchemaVersion(version: number): void {
    try { this.storage.setItem(this.key + '_v', String(version)) }
    catch {}
  }

  saveSchema(properties: Map<number, PropertySchema>): void {
    try {
      const schema: [number, PropertySchema][] = []
      for (const [id, prop] of properties) {
        schema.push([id, prop])
      }
      this.storage.setItem(this.key, JSON.stringify(schema))
    } catch {}
  }

  loadSchema(): [number, PropertySchema][] | null {
    try {
      const raw = this.storage.getItem(this.key)
      if (!raw) return null
      return JSON.parse(raw)
    } catch {
      return null
    }
  }
}
