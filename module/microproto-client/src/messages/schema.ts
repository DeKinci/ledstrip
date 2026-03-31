import { TYPES, COLORS, COLOR_NAMES } from '../wire/constants.js'
import { decodeVarint } from '../wire/varint.js'
import { decodePropId } from '../wire/propid.js'
import { decodeValue, getTypeSize } from '../wire/basic-types.js'
import { getValueSize } from '../wire/typed-value.js'
import type { TypeDef, ValidationConstraints, LengthConstraints, UIHints, PropertySchema, FunctionSchema } from '../types.js'

export type SchemaItem = PropertySchema | FunctionSchema

/** Decode a SCHEMA_UPSERT message. Returns array of schema items. */
export function decodeSchemaUpsert(data: Uint8Array, flags: number): SchemaItem[] {
  const isBatch = !!(flags & 0x01)
  let offset = 1
  let count = 1

  if (isBatch) {
    count = data[offset] + 1
    offset++
  }

  const items: SchemaItem[] = []
  for (let i = 0; i < count && offset < data.length; i++) {
    const result = decodeSchemaItem(data, offset)
    if (!result) break
    items.push(result[0])
    offset = result[1]
  }
  return items
}

/** Decode a single schema item (property or function). Returns [item, newOffset] or null. */
export function decodeSchemaItem(data: Uint8Array, offset: number): [SchemaItem, number] | null {
  if (offset >= data.length) return null

  const itemType = data[offset++]
  const propType = itemType & 0x0f
  const readonly = !!(itemType & 0x10)
  const persistent = !!(itemType & 0x20)
  const hidden = !!(itemType & 0x40)

  if (propType !== 1 && propType !== 2) return null

  // Level flags
  const levelFlags = data[offset++]
  const level = levelFlags & 0x03
  const bleExposed = !!(levelFlags & 0x04)

  let groupId = 0
  if (level === 1) {
    groupId = data[offset++]
  }

  // Item ID
  const [id, idBytes] = decodePropId(data, offset)
  offset += idBytes

  // Namespace ID
  const [namespaceId, nsBytes] = decodePropId(data, offset)
  offset += nsBytes

  // Name (ident: u8 length + ASCII)
  const nameLen = data[offset++]
  const name = new TextDecoder().decode(data.slice(offset, offset + nameLen))
  offset += nameLen

  // Description (varint length + UTF-8)
  const [descLen, descBytes] = decodeVarint(data, offset)
  offset += descBytes
  const description = descLen > 0
    ? new TextDecoder().decode(data.slice(offset, offset + descLen))
    : null
  offset += descLen

  // ===== FUNCTION (type 2) =====
  if (propType === 2) {
    const paramCount = data[offset++]
    const params: Array<{ name: string; typeId: number; typeDef: TypeDef }> = []

    for (let i = 0; i < paramCount; i++) {
      const pNameLen = data[offset++]
      const pName = new TextDecoder().decode(data.slice(offset, offset + pNameLen))
      offset += pNameLen

      const pResult = decodeDataTypeDefinition(data, offset)
      if (!pResult) return null
      offset = pResult[1]

      params.push({ name: pName, typeId: pResult[0].typeId, typeDef: pResult[0] })
    }

    const returnTypeId = data[offset++]
    let returnTypeDef: TypeDef | null = null
    if (returnTypeId !== 0x00) {
      const valFlags = data[offset++]
      returnTypeDef = { typeId: returnTypeId }
    }

    return [{
      id, name, description, type: 'function' as const,
      params, returnTypeId, returnTypeDef,
      bleExposed, namespaceId,
    }, offset]
  }

  // ===== PROPERTY (type 1) =====
  const typeResult = decodeDataTypeDefinition(data, offset)
  if (!typeResult) return null
  const [typeDef, typeOffset] = typeResult
  offset = typeOffset

  // Default value - skip based on type
  const defaultValueSize = getValueSize(typeDef, data, offset)
  offset += defaultValueSize

  // UI hints
  const uiResult = parseUIHints(data, offset)
  offset = uiResult.newOffset

  return [{
    id, name, description,
    typeId: typeDef.typeId,
    constraints: typeDef.constraints || {},
    elementTypeId: typeDef.elementTypeId,
    elementCount: typeDef.elementCount,
    elementTypeDef: typeDef.elementTypeDef,
    elementConstraints: typeDef.elementConstraints || {},
    lengthConstraints: typeDef.lengthConstraints || {},
    fields: typeDef.fields,
    variants: typeDef.variants,
    headerTypeDef: typeDef.headerTypeDef,
    bodyTypeDef: typeDef.bodyTypeDef,
    readonly, persistent, hidden,
    level, groupId, namespaceId, bleExposed,
    ui: uiResult.ui,
    value: null,
  }, offset]
}

/** Decode a recursive DATA_TYPE_DEFINITION. Returns [typeDef, newOffset] or null. */
export function decodeDataTypeDefinition(data: Uint8Array, offset: number): [TypeDef, number] | null {
  if (offset >= data.length) return null

  const typeId = data[offset++]
  const typeDef: TypeDef = { typeId }

  if (typeId >= 0x01 && typeId <= 0x07) {
    const result = parseValidationConstraints(data, offset, typeId)
    typeDef.constraints = result.constraints
    offset = result.newOffset
  } else if (typeId === TYPES.ARRAY) {
    const [count, countBytes] = decodeVarint(data, offset)
    typeDef.elementCount = count
    offset += countBytes

    const elemResult = decodeDataTypeDefinition(data, offset)
    if (!elemResult) return null
    typeDef.elementTypeDef = elemResult[0]
    typeDef.elementTypeId = elemResult[0].typeId
    typeDef.elementConstraints = elemResult[0].constraints || {}
    offset = elemResult[1]
  } else if (typeId === TYPES.LIST) {
    const lenResult = parseLengthConstraints(data, offset)
    typeDef.lengthConstraints = lenResult.lengthConstraints
    offset = lenResult.newOffset

    const elemResult = decodeDataTypeDefinition(data, offset)
    if (!elemResult) return null
    typeDef.elementTypeDef = elemResult[0]
    typeDef.elementTypeId = elemResult[0].typeId
    typeDef.elementConstraints = elemResult[0].constraints || {}
    offset = elemResult[1]
  } else if (typeId === TYPES.OBJECT) {
    const [fieldCount, fcBytes] = decodeVarint(data, offset)
    offset += fcBytes

    typeDef.fields = []
    for (let i = 0; i < fieldCount; i++) {
      const nameLen = data[offset++]
      const fieldName = new TextDecoder().decode(data.slice(offset, offset + nameLen))
      offset += nameLen

      const fieldTypeResult = decodeDataTypeDefinition(data, offset)
      if (!fieldTypeResult) return null
      offset = fieldTypeResult[1]

      typeDef.fields.push({ name: fieldName, typeDef: fieldTypeResult[0] })
    }
  } else if (typeId === TYPES.VARIANT) {
    const [typeCount, tcBytes] = decodeVarint(data, offset)
    offset += tcBytes

    typeDef.variants = []
    for (let i = 0; i < typeCount; i++) {
      const [nameLen, nlBytes] = decodeVarint(data, offset)
      offset += nlBytes
      const variantName = new TextDecoder().decode(data.slice(offset, offset + nameLen))
      offset += nameLen

      const variantTypeResult = decodeDataTypeDefinition(data, offset)
      if (!variantTypeResult) return null
      offset = variantTypeResult[1]

      typeDef.variants.push({ name: variantName, typeDef: variantTypeResult[0] })
    }
  } else if (typeId === TYPES.RESOURCE) {
    const headerResult = decodeDataTypeDefinition(data, offset)
    if (!headerResult) return null
    typeDef.headerTypeDef = headerResult[0]
    offset = headerResult[1]

    const bodyResult = decodeDataTypeDefinition(data, offset)
    if (!bodyResult) return null
    typeDef.bodyTypeDef = bodyResult[0]
    offset = bodyResult[1]
  } else if (typeId === TYPES.STREAM) {
    const [capacity, capBytes] = decodeVarint(data, offset)
    typeDef.historyCapacity = capacity
    offset += capBytes

    const elemResult = decodeDataTypeDefinition(data, offset)
    if (!elemResult) return null
    typeDef.elementTypeDef = elemResult[0]
    typeDef.elementTypeId = elemResult[0].typeId
    offset = elemResult[1]
  }

  return [typeDef, offset]
}

export function parseValidationConstraints(data: Uint8Array, offset: number, typeId: number): { constraints: ValidationConstraints; newOffset: number } {
  const constraints: ValidationConstraints = { hasMin: false, hasMax: false, hasStep: false, hasOneOf: false }
  if (offset >= data.length) return { constraints, newOffset: offset }

  const flags = data[offset++]
  const typeSize = getTypeSize(typeId)
  const view = new DataView(data.buffer, data.byteOffset)

  if (flags & 0x01) {
    constraints.hasMin = true
    const [val] = decodeValue(view, offset, typeId)
    constraints.min = val
    offset += typeSize
  }
  if (flags & 0x02) {
    constraints.hasMax = true
    const [val] = decodeValue(view, offset, typeId)
    constraints.max = val
    offset += typeSize
  }
  if (flags & 0x04) {
    constraints.hasStep = true
    const [val] = decodeValue(view, offset, typeId)
    constraints.step = val
    offset += typeSize
  }
  if (flags & 0x08) {
    constraints.hasOneOf = true
    const [count, countBytes] = decodeVarint(data, offset)
    offset += countBytes
    constraints.oneOf = []
    for (let i = 0; i < count; i++) {
      const [val] = decodeValue(view, offset, typeId)
      constraints.oneOf.push(val as number)
      offset += typeSize
    }
  }

  return { constraints, newOffset: offset }
}

export function parseLengthConstraints(data: Uint8Array, offset: number): { lengthConstraints: LengthConstraints; newOffset: number } {
  const lengthConstraints: LengthConstraints = { hasMinLength: false, hasMaxLength: false }
  if (offset >= data.length) return { lengthConstraints, newOffset: offset }

  const flags = data[offset++]

  if (flags & 0x01) {
    lengthConstraints.hasMinLength = true
    const [val, bytes] = decodeVarint(data, offset)
    lengthConstraints.minLength = val
    offset += bytes
  }
  if (flags & 0x02) {
    lengthConstraints.hasMaxLength = true
    const [val, bytes] = decodeVarint(data, offset)
    lengthConstraints.maxLength = val
    offset += bytes
  }

  return { lengthConstraints, newOffset: offset }
}

export function parseUIHints(data: Uint8Array, offset: number): { ui: UIHints; newOffset: number } {
  const ui: UIHints = { color: null, colorHex: null, unit: null, icon: null, widget: 0 }
  if (offset >= data.length) return { ui, newOffset: offset }

  const flags = data[offset++]
  const hasWidget = !!(flags & 0x01)
  const hasUnit = !!(flags & 0x02)
  const hasIcon = !!(flags & 0x04)
  const colorGroup = (flags >> 4) & 0x0f

  if (colorGroup > 0) {
    ui.color = COLOR_NAMES[colorGroup] || null
    ui.colorHex = COLORS[colorGroup] || null
  }

  if (hasWidget && offset < data.length) {
    ui.widget = data[offset++]
  }

  if (hasUnit && offset < data.length) {
    const unitLen = data[offset++]
    if (unitLen > 0 && offset + unitLen <= data.length) {
      ui.unit = new TextDecoder().decode(data.slice(offset, offset + unitLen))
      offset += unitLen
    }
  }

  if (hasIcon && offset < data.length) {
    const iconLen = data[offset++]
    if (iconLen > 0 && offset + iconLen <= data.length) {
      ui.icon = new TextDecoder().decode(data.slice(offset, offset + iconLen))
      offset += iconLen
    }
  }

  return { ui, newOffset: offset }
}
