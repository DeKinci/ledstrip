export { MicroProtoClient } from './client.js'
export { OPCODES, TYPES, ERROR_CODES, WIDGETS, COLORS, COLOR_NAMES, PROTOCOL_VERSION } from './wire/constants.js'
export { MemoryStorage, SchemaCache, hashUrl } from './schema-cache.js'

// Wire-level exports for CLI/debugger usage
export { encodeVarint, decodeVarint, varintSize } from './wire/varint.js'
export { encodePropId, decodePropId } from './wire/propid.js'
export { encodeValue, decodeValue, getTypeSize } from './wire/basic-types.js'
export { encodeTypedValue, decodeTypedValue, getValueSize } from './wire/typed-value.js'

// Message-level exports
export { encodeHello, decodeHelloResponse } from './messages/hello.js'
export { decodeSchemaUpsert, decodeSchemaItem, decodeDataTypeDefinition, parseUIHints, parseValidationConstraints, parseLengthConstraints } from './messages/schema.js'
export { encodePropertyUpdate, decodePropertyUpdates } from './messages/property-update.js'
export { encodePing, encodePong } from './messages/ping.js'
export { decodeError } from './messages/error.js'
export { encodeRpcRequest, decodeRpcResponse, decodeRpcRequest } from './messages/rpc.js'
export { encodeResourceGet, encodeResourcePut, encodeResourceDelete, decodeResourceGetResponse, decodeResourcePutResponse, decodeResourceDeleteResponse } from './messages/resource.js'

// Type exports
export type {
  TypeDef, ValidationConstraints, LengthConstraints, UIHints,
  PropertySchema, FunctionSchema, ClientOptions,
  WebSocketLike, StorageLike,
} from './types.js'
