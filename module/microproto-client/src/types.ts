/** Type definition for a single type in the schema (recursive for composites) */
export interface TypeDef {
  typeId: number
  // Basic type constraints
  constraints?: ValidationConstraints
  // ARRAY
  elementCount?: number
  elementTypeDef?: TypeDef
  elementTypeId?: number
  elementConstraints?: ValidationConstraints
  // LIST
  lengthConstraints?: LengthConstraints
  // OBJECT
  fields?: Array<{ name: string; typeDef: TypeDef }>
  // VARIANT
  variants?: Array<{ name: string; typeDef: TypeDef }>
  // RESOURCE
  headerTypeDef?: TypeDef
  bodyTypeDef?: TypeDef
  // STREAM
  historyCapacity?: number
}

export interface ValidationConstraints {
  hasMin?: boolean; min?: number
  hasMax?: boolean; max?: number
  hasStep?: boolean; step?: number
  hasOneOf?: boolean; oneOf?: number[]
}

export interface LengthConstraints {
  hasMinLength?: boolean; minLength?: number
  hasMaxLength?: boolean; maxLength?: number
}

export interface UIHints {
  color: string | null
  colorHex: string | null
  unit: string | null
  icon: string | null
  widget: number
}

export interface PropertySchema {
  id: number
  name: string
  description: string | null
  typeId: number
  constraints: ValidationConstraints
  elementTypeId?: number
  elementCount?: number
  elementTypeDef?: TypeDef
  elementConstraints?: ValidationConstraints
  lengthConstraints?: LengthConstraints
  fields?: Array<{ name: string; typeDef: TypeDef }>
  variants?: Array<{ name: string; typeDef: TypeDef }>
  headerTypeDef?: TypeDef
  bodyTypeDef?: TypeDef
  readonly: boolean
  persistent: boolean
  hidden: boolean
  level: number
  groupId: number
  namespaceId: number
  bleExposed: boolean
  ui: UIHints
  value: any
}

export interface FunctionSchema {
  id: number
  name: string
  description: string | null
  type: 'function'
  params: Array<{ name: string; typeId: number; typeDef: TypeDef }>
  returnTypeId: number
  returnTypeDef: TypeDef | null
  bleExposed: boolean
  namespaceId: number
}

export interface ClientOptions {
  deviceId?: number
  maxPacketSize?: number
  reconnect?: boolean
  reconnectDelay?: number
  heartbeatInterval?: number
  heartbeatTimeout?: number
  rpcTimeout?: number
  resourceTimeout?: number
  storageKey?: string
  debug?: boolean
  webSocketFactory?: (url: string) => WebSocketLike
  storage?: StorageLike
}

export interface WebSocketLike {
  readonly readyState: number
  binaryType: string
  onopen: ((ev: any) => void) | null
  onclose: ((ev: any) => void) | null
  onmessage: ((ev: any) => void) | null
  onerror: ((ev: any) => void) | null
  send(data: ArrayBuffer | Uint8Array): void
  close(): void
}

export interface StorageLike {
  getItem(key: string): string | null
  setItem(key: string, value: string): void
  removeItem(key: string): void
}

export const WS_OPEN = 1
