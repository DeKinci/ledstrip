import {
  OPCODES, TYPES, ERROR_CODES, COLOR_NAMES, WIDGETS,
  decodeVarint, decodePropId, decodeValue, getTypeSize,
  decodeHelloResponse, decodeSchemaUpsert, decodePropertyUpdates,
  decodeError, decodeRpcResponse, decodeRpcRequest,
  decodeResourceGetResponse, decodeResourcePutResponse, decodeResourceDeleteResponse,
  type PropertySchema, type FunctionSchema,
} from '@microproto/client'

// ANSI colors
const RESET = '\x1b[0m'
const BOLD = '\x1b[1m'
const DIM = '\x1b[2m'
const RED = '\x1b[31m'
const GREEN = '\x1b[32m'
const YELLOW = '\x1b[33m'
const BLUE = '\x1b[34m'
const MAGENTA = '\x1b[35m'
const CYAN = '\x1b[36m'
const WHITE = '\x1b[37m'
const GRAY = '\x1b[90m'

const OPCODE_NAMES: Record<number, string> = {
  [OPCODES.HELLO]: 'HELLO',
  [OPCODES.PROPERTY_UPDATE]: 'UPDATE',
  [OPCODES.SCHEMA_UPSERT]: 'SCHEMA',
  [OPCODES.SCHEMA_DELETE]: 'SCHEMA_DEL',
  [OPCODES.RPC]: 'RPC',
  [OPCODES.PING]: 'PING',
  [OPCODES.ERROR]: 'ERROR',
  [OPCODES.RESOURCE_GET]: 'RES_GET',
  [OPCODES.RESOURCE_PUT]: 'RES_PUT',
  [OPCODES.RESOURCE_DELETE]: 'RES_DEL',
}

const TYPE_NAMES: Record<number, string> = {
  [TYPES.BOOL]: 'BOOL',
  [TYPES.INT8]: 'INT8',
  [TYPES.UINT8]: 'UINT8',
  [TYPES.INT32]: 'INT32',
  [TYPES.FLOAT32]: 'FLOAT32',
  [TYPES.INT16]: 'INT16',
  [TYPES.UINT16]: 'UINT16',
  [TYPES.ARRAY]: 'ARRAY',
  [TYPES.LIST]: 'LIST',
  [TYPES.OBJECT]: 'OBJECT',
  [TYPES.VARIANT]: 'VARIANT',
  [TYPES.RESOURCE]: 'RESOURCE',
  [TYPES.STREAM]: 'STREAM',
}

function timestamp(): string {
  const now = new Date()
  const h = String(now.getHours()).padStart(2, '0')
  const m = String(now.getMinutes()).padStart(2, '0')
  const s = String(now.getSeconds()).padStart(2, '0')
  const ms = String(now.getMilliseconds()).padStart(3, '0')
  return `${GRAY}[${h}:${m}:${s}.${ms}]${RESET}`
}

function formatValue(value: any): string {
  if (value === true) return `${GREEN}true${RESET}`
  if (value === false) return `${RED}false${RESET}`
  if (typeof value === 'number') return `${YELLOW}${value}${RESET}`
  if (Array.isArray(value)) {
    if (value.length <= 8) return `[${value.map(formatValue).join(', ')}]`
    return `[${value.length} items]`
  }
  if (value && typeof value === 'object') {
    if (value._type !== undefined) return `${MAGENTA}${value._type}${RESET}(${formatValue(value.value)})`
    const entries = Object.entries(value).map(([k, v]) => `${k}=${formatValue(v)}`).join(' ')
    return `{${entries}}`
  }
  return String(value)
}

export class Formatter {
  private properties = new Map<number, PropertySchema>()
  private propertyByName = new Map<string, number>()
  private functions = new Map<number, FunctionSchema>()

  formatSent(data: Uint8Array): string {
    const opcode = data[0] & 0x0f
    const flags = (data[0] >> 4) & 0x0f
    const name = OPCODE_NAMES[opcode] || `OP_${opcode}`

    let detail = ''

    if (opcode === OPCODES.HELLO) {
      detail = this.formatHelloRequest(data)
    } else if (opcode === OPCODES.PROPERTY_UPDATE) {
      detail = this.formatSentPropertyUpdate(data)
    } else if (opcode === OPCODES.PING) {
      const isResponse = !!(flags & 0x01)
      detail = isResponse ? '(pong)' : `(payload=${data[1] || 0})`
    } else if (opcode === OPCODES.RPC) {
      detail = this.formatSentRpc(data, flags)
    }

    return `${timestamp()} ${CYAN}>>>${RESET} ${BOLD}${name}${RESET} ${detail}`
  }

  formatReceived(data: Uint8Array): string {
    const opcode = data[0] & 0x0f
    const flags = (data[0] >> 4) & 0x0f
    const name = OPCODE_NAMES[opcode] || `OP_${opcode}`
    const lines: string[] = []

    let detail = ''

    switch (opcode) {
      case OPCODES.HELLO:
        detail = this.formatHelloResponse(data)
        break
      case OPCODES.SCHEMA_UPSERT:
        return this.formatSchemaUpsert(data, flags)
      case OPCODES.SCHEMA_DELETE:
        detail = `delete ${!!(flags & 0x01) ? 'batch' : 'single'}`
        break
      case OPCODES.PROPERTY_UPDATE:
        return this.formatPropertyUpdate(data, flags)
      case OPCODES.RPC:
        detail = this.formatRpcMessage(data, flags)
        break
      case OPCODES.PING:
        detail = (flags & 0x01) ? '(pong)' : '(request)'
        break
      case OPCODES.ERROR:
        detail = this.formatErrorMessage(data, flags)
        break
      case OPCODES.RESOURCE_GET:
      case OPCODES.RESOURCE_PUT:
      case OPCODES.RESOURCE_DELETE:
        detail = this.formatResourceResponse(opcode, data, flags)
        break
    }

    return `${timestamp()} ${GREEN}<<<${RESET} ${BOLD}${name}${RESET} ${detail}`
  }

  private formatHelloRequest(data: Uint8Array): string {
    let offset = 1
    const version = data[offset++]
    const [maxPkt, mpBytes] = decodeVarint(data, offset)
    offset += mpBytes
    const [devId, diBytes] = decodeVarint(data, offset)
    offset += diBytes
    const sv = data[offset] | (data[offset + 1] << 8)
    return `${DIM}v=${version} maxPkt=${maxPkt} devId=0x${devId.toString(16)} schema=${sv}${RESET}`
  }

  private formatHelloResponse(data: Uint8Array): string {
    const resp = decodeHelloResponse(data)
    if (!resp) return `${RED}(invalid)${RESET}`
    return `${DIM}v=${resp.version} session=${resp.sessionId} schema=${resp.schemaVersion} maxPkt=${resp.maxPacket}${RESET}`
  }

  private formatSchemaUpsert(data: Uint8Array, flags: number): string {
    const items = decodeSchemaUpsert(data, flags)
    const lines = [`${timestamp()} ${GREEN}<<<${RESET} ${BOLD}SCHEMA${RESET} batch(${items.length})`]

    for (const item of items) {
      if ('type' in item && item.type === 'function') {
        const func = item as FunctionSchema
        this.functions.set(func.id, func)
        const params = func.params.map(p => `${p.name}:${TYPE_NAMES[p.typeId] || '?'}`).join(', ')
        const ret = func.returnTypeId ? TYPE_NAMES[func.returnTypeId] || '?' : 'void'
        lines.push(`  ${MAGENTA}fn${RESET} [${func.id}] ${BOLD}${func.name}${RESET}(${params}) -> ${ret}`)
      } else {
        const prop = item as PropertySchema
        this.properties.set(prop.id, prop)
        this.propertyByName.set(prop.name, prop.id)

        const typeName = TYPE_NAMES[prop.typeId] || `0x${prop.typeId.toString(16)}`
        const parts: string[] = [typeName]

        if (prop.constraints.hasMin || prop.constraints.hasMax) {
          const min = prop.constraints.hasMin ? prop.constraints.min : ''
          const max = prop.constraints.hasMax ? prop.constraints.max : ''
          parts.push(`[${min}..${max}]`)
        }
        if (prop.ui.widget) parts.push(Object.entries(WIDGETS).find(([, v]) => v === prop.ui.widget)?.[0]?.toLowerCase() || `w${prop.ui.widget}`)
        if (prop.ui.color) parts.push(String(prop.ui.color))
        if (prop.readonly) parts.push(`${RED}readonly${RESET}`)

        lines.push(`  ${BLUE}[${prop.id}]${RESET} ${BOLD}${prop.name}${RESET}: ${parts.join(' ')}`)
      }
    }

    return lines.join('\n')
  }

  private formatPropertyUpdate(data: Uint8Array, flags: number): string {
    const { updates, timestamp: ts } = decodePropertyUpdates(
      data, flags, (id) => this.properties.get(id),
    )

    if (updates.length === 0) {
      return `${timestamp()} ${GREEN}<<<${RESET} ${BOLD}UPDATE${RESET} ${RED}(unknown properties)${RESET}`
    }

    if (updates.length === 1) {
      const { propertyId, value } = updates[0]
      const prop = this.properties.get(propertyId)
      const name = prop?.name || `prop_${propertyId}`
      return `${timestamp()} ${GREEN}<<<${RESET} ${BOLD}UPDATE${RESET} ${name} = ${formatValue(value)}`
    }

    const lines = [`${timestamp()} ${GREEN}<<<${RESET} ${BOLD}UPDATE${RESET} batch(${updates.length})${ts ? ` ts=${ts}` : ''}`]
    for (const { propertyId, value } of updates) {
      const prop = this.properties.get(propertyId)
      const name = prop?.name || `prop_${propertyId}`
      lines.push(`  ${name} = ${formatValue(value)}`)
    }
    return lines.join('\n')
  }

  private formatSentPropertyUpdate(data: Uint8Array): string {
    let offset = 1
    const [propId, idBytes] = decodePropId(data, offset)
    const prop = this.properties.get(propId)
    const name = prop?.name || `prop_${propId}`
    return `${name} (${data.length - 1 - idBytes} bytes)`
  }

  private formatSentRpc(data: Uint8Array, flags: number): string {
    const needsResponse = !!(flags & 0x02)
    let offset = 1
    const [funcId, fidBytes] = decodePropId(data, offset)
    offset += fidBytes
    const func = this.functions.get(funcId)
    const name = func?.name || `func_${funcId}`
    return `${name} ${needsResponse ? '(await)' : '(fire&forget)'}`
  }

  private formatRpcMessage(data: Uint8Array, flags: number): string {
    const isResponse = !!(flags & 0x01)
    if (isResponse) {
      const success = !!(flags & 0x02)
      const hasValue = !!(flags & 0x04)
      const callId = data[1]
      return `response callId=${callId} ${success ? `${GREEN}ok${RESET}` : `${RED}err${RESET}`}${hasValue ? ' +value' : ''}`
    }
    const req = decodeRpcRequest(data, flags)
    const func = this.functions.get(req.functionId)
    return `request ${func?.name || `func_${req.functionId}`}${req.needsResponse ? ` callId=${req.callId}` : ''}`
  }

  private formatErrorMessage(data: Uint8Array, flags: number): string {
    const err = decodeError(data, flags)
    const codeName = Object.entries(ERROR_CODES).find(([, v]) => v === err.code)?.[0] || `0x${err.code.toString(16)}`
    return `${RED}${codeName}: ${err.message}${RESET}${err.schemaMismatch ? ` ${YELLOW}[schema_mismatch]${RESET}` : ''}`
  }

  private formatResourceResponse(opcode: number, data: Uint8Array, flags: number): string {
    const isError = !!(flags & 0x02)
    const reqId = data[1]
    if (isError) return `${RED}error${RESET} reqId=${reqId}`
    return `${GREEN}ok${RESET} reqId=${reqId}`
  }
}
