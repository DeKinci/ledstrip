import { TYPES } from '@microproto/client'

/** Re-export type IDs for widget convenience */
export const TYPE = TYPES

/** Property schema as received from MicroProto (widget-facing subset) */
export interface PropertySchema {
  id: number
  name: string
  typeId: number
  readonly: boolean
  persistent: boolean
  hidden: boolean
  description?: string
  constraints: {
    hasMin?: boolean; min?: number
    hasMax?: boolean; max?: number
    hasStep?: boolean; step?: number
  }
  ui: {
    widget: number
    color: string | null
    colorHex?: string | null
    unit?: string | null
    icon?: string | null
  }
  // For containers
  elementTypeId?: number
  elementTypeDef?: any
  elementCount?: number
  // For RESOURCE
  headerTypeDef?: any
  bodyTypeDef?: any
  // For OBJECT
  fields?: Array<{ name: string; typeDef: any }>
}

/** MicroProto client interface (subset needed by widgets) */
export interface MicroProtoClient {
  on(event: 'property', cb: (id: number, name: string, value: any, oldValue: any) => void): void
  on(event: 'schema', cb: (prop: PropertySchema) => void): void
  on(event: 'ready', cb: () => void): void
  on(event: 'connect', cb: () => void): void
  on(event: 'disconnect', cb: () => void): void
  on(event: string, cb: (...args: any[]) => void): void
  setProperty(name: string, value: any): boolean
  getProperty(name: string): any
  getResource(name: string, id: number): Promise<{ data: Uint8Array }>
  putResource(name: string, id: number, data: { header: Uint8Array; body: Uint8Array }): Promise<{ resourceId: number }>
  deleteResource(name: string, id: number): Promise<void>
  connect(): void
  isConnected(): boolean
}

/** Widget instance — created per property, manages its own DOM */
export interface Widget {
  element: HTMLElement
  update(value: any, oldValue?: any): void
  destroy?(): void
}

/** Widget factory — registered in the registry */
export interface WidgetFactory {
  types: number[]
  widgetId: number
  complex?: boolean
  create(schema: PropertySchema, client: MicroProtoClient): Widget
}
