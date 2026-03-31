import { OPCODES } from '../wire/constants.js'
import { encodeVarint, decodeVarint } from '../wire/varint.js'
import { encodePropId } from '../wire/propid.js'

/** Encode a RESOURCE_GET request. */
export function encodeResourceGet(requestId: number, propertyId: number, resourceId: number): ArrayBuffer {
  const propIdBytes = encodePropId(propertyId)
  const resourceIdBytes = encodeVarint(resourceId)

  const buf = new ArrayBuffer(1 + 1 + propIdBytes.length + resourceIdBytes.length)
  const bytes = new Uint8Array(buf)
  let offset = 0

  bytes[offset++] = OPCODES.RESOURCE_GET
  bytes[offset++] = requestId
  bytes.set(propIdBytes, offset)
  offset += propIdBytes.length
  bytes.set(resourceIdBytes, offset)

  return buf
}

/** Encode a RESOURCE_PUT request. */
export function encodeResourcePut(
  requestId: number,
  propertyId: number,
  resourceId: number,
  options: { header?: Uint8Array; body?: Uint8Array } = {},
): ArrayBuffer {
  const propIdBytes = encodePropId(propertyId)
  const resourceIdBytes = encodeVarint(resourceId)

  let flags = 0
  if (options.header) flags |= 0x02
  if (options.body) flags |= 0x04

  const headerLenBytes = options.header ? encodeVarint(options.header.length) : new Uint8Array(0)
  const bodyLenBytes = options.body ? encodeVarint(options.body.length) : new Uint8Array(0)

  let messageSize = 1 + 1 + propIdBytes.length + resourceIdBytes.length
  if (options.header) messageSize += headerLenBytes.length + options.header.length
  if (options.body) messageSize += bodyLenBytes.length + options.body.length

  const buf = new ArrayBuffer(messageSize)
  const bytes = new Uint8Array(buf)
  let offset = 0

  bytes[offset++] = OPCODES.RESOURCE_PUT | (flags << 4)
  bytes[offset++] = requestId
  bytes.set(propIdBytes, offset)
  offset += propIdBytes.length
  bytes.set(resourceIdBytes, offset)
  offset += resourceIdBytes.length

  if (options.header) {
    bytes.set(headerLenBytes, offset)
    offset += headerLenBytes.length
    bytes.set(options.header, offset)
    offset += options.header.length
  }
  if (options.body) {
    bytes.set(bodyLenBytes, offset)
    offset += bodyLenBytes.length
    bytes.set(options.body, offset)
  }

  return buf
}

/** Encode a RESOURCE_DELETE request. */
export function encodeResourceDelete(requestId: number, propertyId: number, resourceId: number): ArrayBuffer {
  const propIdBytes = encodePropId(propertyId)
  const resourceIdBytes = encodeVarint(resourceId)

  const buf = new ArrayBuffer(1 + 1 + propIdBytes.length + resourceIdBytes.length)
  const bytes = new Uint8Array(buf)
  let offset = 0

  bytes[offset++] = OPCODES.RESOURCE_DELETE
  bytes[offset++] = requestId
  bytes.set(propIdBytes, offset)
  offset += propIdBytes.length
  bytes.set(resourceIdBytes, offset)

  return buf
}

export interface ResourceResponse {
  requestId: number
  success: boolean
  data?: Uint8Array
  resourceId?: number
  errorCode?: number
  errorMessage?: string
}

/** Decode a RESOURCE_GET response. */
export function decodeResourceGetResponse(data: Uint8Array, flags: number): ResourceResponse {
  const isError = !!(flags & 0x02)
  let offset = 1
  const requestId = data[offset++]

  if (isError) {
    return decodeResourceError(data, offset, requestId)
  }

  const [bodyLen, lenBytes] = decodeVarint(data, offset)
  offset += lenBytes
  return { requestId, success: true, data: data.slice(offset, offset + bodyLen) }
}

/** Decode a RESOURCE_PUT response. */
export function decodeResourcePutResponse(data: Uint8Array, flags: number): ResourceResponse {
  const isError = !!(flags & 0x02)
  let offset = 1
  const requestId = data[offset++]

  if (isError) {
    return decodeResourceError(data, offset, requestId)
  }

  const [resourceId] = decodeVarint(data, offset)
  return { requestId, success: true, resourceId }
}

/** Decode a RESOURCE_DELETE response. */
export function decodeResourceDeleteResponse(data: Uint8Array, flags: number): ResourceResponse {
  const isError = !!(flags & 0x02)
  let offset = 1
  const requestId = data[offset++]

  if (isError) {
    return decodeResourceError(data, offset, requestId)
  }

  return { requestId, success: true }
}

function decodeResourceError(data: Uint8Array, offset: number, requestId: number): ResourceResponse {
  const errorCode = data[offset++]
  const [msgLen, msgBytes] = decodeVarint(data, offset)
  offset += msgBytes
  const errorMessage = new TextDecoder().decode(data.slice(offset, offset + msgLen))
  return { requestId, success: false, errorCode, errorMessage }
}
