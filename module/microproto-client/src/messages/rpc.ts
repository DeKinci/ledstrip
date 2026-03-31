import { OPCODES } from '../wire/constants.js'
import { decodeVarint } from '../wire/varint.js'
import { encodePropId, decodePropId } from '../wire/propid.js'
import { decodeTypedValue } from '../wire/typed-value.js'
import type { TypeDef } from '../types.js'

/** Encode an RPC request. */
export function encodeRpcRequest(
  functionId: number,
  callId: number | null,
  needsResponse: boolean,
  params?: Uint8Array | ArrayBuffer | null,
): ArrayBuffer {
  const flags = needsResponse ? 0x02 : 0x00
  const funcIdBytes = encodePropId(functionId)
  const paramData = params ? new Uint8Array(params) : new Uint8Array(0)

  let messageSize = 1 + funcIdBytes.length + paramData.length
  if (needsResponse) messageSize += 1

  const buf = new ArrayBuffer(messageSize)
  const bytes = new Uint8Array(buf)
  let offset = 0

  bytes[offset++] = OPCODES.RPC | (flags << 4)
  bytes.set(funcIdBytes, offset)
  offset += funcIdBytes.length

  if (needsResponse && callId !== null) {
    bytes[offset++] = callId
  }

  bytes.set(paramData, offset)
  return buf
}

export interface RpcResponse {
  callId: number
  success: boolean
  value?: any
  data?: Uint8Array
  errorCode?: number
  errorMessage?: string
}

/** Decode an RPC response message. */
export function decodeRpcResponse(
  data: Uint8Array,
  flags: number,
  getReturnTypeDef?: (callId: number) => TypeDef | null,
): RpcResponse {
  const success = !!(flags & 0x02)
  const hasReturnValue = !!(flags & 0x04)

  let offset = 1
  const callId = data[offset++]

  if (success) {
    if (hasReturnValue) {
      const returnTypeDef = getReturnTypeDef?.(callId)
      if (returnTypeDef) {
        const view = new DataView(data.buffer, data.byteOffset)
        const [value] = decodeTypedValue(view, data, offset, returnTypeDef)
        return { callId, success: true, value }
      }
      return { callId, success: true, data: data.slice(offset) }
    }
    return { callId, success: true }
  }

  // Error response
  const errorCode = data[offset++]
  const [msgLen, msgBytes] = decodeVarint(data, offset)
  offset += msgBytes
  const errorMessage = new TextDecoder().decode(data.slice(offset, offset + msgLen))

  return { callId, success: false, errorCode, errorMessage }
}

export interface RpcRequest {
  functionId: number
  callId: number | null
  needsResponse: boolean
  data: Uint8Array
}

/** Decode an incoming RPC request (server calling client). */
export function decodeRpcRequest(data: Uint8Array, flags: number): RpcRequest {
  const needsResponse = !!(flags & 0x02)

  let offset = 1
  const [functionId, fidBytes] = decodePropId(data, offset)
  offset += fidBytes

  let callId: number | null = null
  if (needsResponse) {
    callId = data[offset++]
  }

  return { functionId, callId, needsResponse, data: data.slice(offset) }
}
