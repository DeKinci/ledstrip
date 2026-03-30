/** MicroProto type IDs */
export const TYPE = {
  BOOL: 0x01,
  INT8: 0x02,
  UINT8: 0x03,
  INT32: 0x04,
  FLOAT32: 0x05,
  INT16: 0x06,
  UINT16: 0x07,
  ARRAY: 0x20,
  LIST: 0x21,
  OBJECT: 0x22,
  VARIANT: 0x23,
  RESOURCE: 0x24,
} as const

/** Property schema as received from MicroProto */
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
    color: number
    colorHex?: string
    unit?: string
    icon?: string
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
  /** Root DOM element */
  element: HTMLElement
  /** Called when property value changes */
  update(value: any, oldValue?: any): void
  /** Clean up resources */
  destroy?(): void
}

/** Widget factory — registered in the registry */
export interface WidgetFactory {
  /** Which property type(s) this widget handles */
  types: number[]
  /** Which widget ID within that type (0 = default) */
  widgetId: number
  /** Complex widgets go in the right column in two-column layout */
  complex?: boolean
  /** Create a widget instance */
  create(schema: PropertySchema, client: MicroProtoClient): Widget
}
