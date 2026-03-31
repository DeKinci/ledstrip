import { describe, it, expect } from 'vitest'
import { decodeSchemaUpsert, decodeSchemaItem, decodeDataTypeDefinition, parseUIHints, parseValidationConstraints, TYPES } from '@microproto/client'
import type { PropertySchema, FunctionSchema } from '@microproto/client'

const enc = new TextEncoder()

/** Build a minimal SCHEMA_UPSERT for a basic property */
function buildPropertySchema(id: number, name: string, typeId: number, opts: {
  defaultValue?: number[]
  validationFlags?: number
  uiHints?: number[]
  readonly?: boolean
  persistent?: boolean
  hidden?: boolean
  bleExposed?: boolean
} = {}) {
  const nameBytes = enc.encode(name)
  const defaultValue = opts.defaultValue ?? [0]
  const uiHints = opts.uiHints ?? [0]
  const itemFlags = 0x01 // PROPERTY
    | (opts.readonly ? 0x10 : 0)
    | (opts.persistent ? 0x20 : 0)
    | (opts.hidden ? 0x40 : 0)
  const levelFlags = opts.bleExposed ? 0x04 : 0x00

  return new Uint8Array([
    0x03,                  // SCHEMA_UPSERT opcode
    itemFlags,             // item type + flags
    levelFlags,            // level flags
    id,                    // item ID
    0,                     // namespace ID
    nameBytes.length,      // name length
    ...nameBytes,
    0,                     // description length (varint 0)
    typeId,                // type ID
    opts.validationFlags ?? 0, // validation flags
    ...defaultValue,       // default value
    ...uiHints,            // UI hints
  ])
}

describe('decodeSchemaUpsert', () => {
  it('decodes single property', () => {
    const msg = buildPropertySchema(3, 'brightness', TYPES.UINT8, { defaultValue: [128] })
    const items = decodeSchemaUpsert(msg, 0)
    expect(items.length).toBe(1)

    const prop = items[0] as PropertySchema
    expect(prop.id).toBe(3)
    expect(prop.name).toBe('brightness')
    expect(prop.typeId).toBe(TYPES.UINT8)
    expect(prop.readonly).toBe(false)
    expect(prop.value).toBeNull()
  })

  it('decodes batched properties', () => {
    const name1 = enc.encode('a')
    const name2 = enc.encode('b')
    const msg = new Uint8Array([
      0x13,              // SCHEMA_UPSERT with batch flag
      1,                 // count-1 = 1 (2 items)
      0x01, 0x00, 1, 0, name1.length, ...name1, 0, TYPES.BOOL, 0, 0, 0,
      0x01, 0x00, 2, 0, name2.length, ...name2, 0, TYPES.UINT8, 0, 0, 0,
    ])
    const items = decodeSchemaUpsert(msg, 0x01)
    expect(items.length).toBe(2)
    expect(items[0].name).toBe('a')
    expect(items[1].name).toBe('b')
  })

  it('decodes readonly and persistent flags', () => {
    const msg = buildPropertySchema(1, 'test', TYPES.UINT8, { readonly: true, persistent: true })
    const items = decodeSchemaUpsert(msg, 0)
    const prop = items[0] as PropertySchema
    expect(prop.readonly).toBe(true)
    expect(prop.persistent).toBe(true)
  })

  it('decodes hidden flag', () => {
    const msg = buildPropertySchema(1, 'test', TYPES.UINT8, { hidden: true })
    const prop = decodeSchemaUpsert(msg, 0)[0] as PropertySchema
    expect(prop.hidden).toBe(true)
  })

  it('decodes ble_exposed flag', () => {
    const msg = buildPropertySchema(1, 'test', TYPES.UINT8, { bleExposed: true })
    const prop = decodeSchemaUpsert(msg, 0)[0] as PropertySchema
    expect(prop.bleExposed).toBe(true)
  })
})

describe('schema UI hints', () => {
  it('decodes color, widget, unit, icon', () => {
    const unit = enc.encode('ms')
    const icon = enc.encode('\u26A1') // ⚡
    const msg = buildPropertySchema(5, 'speed', TYPES.UINT8, {
      defaultValue: [100],
      uiHints: [
        0x27,             // colorgroup=2 (AMBER) | hasWidget | hasUnit | hasIcon
        1,                // widget: SLIDER
        unit.length, ...unit,
        icon.length, ...icon,
      ],
    })

    const prop = decodeSchemaUpsert(msg, 0)[0] as PropertySchema
    expect(prop.ui.color).toBe('amber')
    expect(prop.ui.colorHex).toBe('#fcd34d')
    expect(prop.ui.widget).toBe(1)
    expect(prop.ui.unit).toBe('ms')
    expect(prop.ui.icon).toBe('\u26A1')
  })

  it('decodes partial hints (unit only)', () => {
    const unit = enc.encode('\u00B0C') // °C
    const msg = buildPropertySchema(7, 'temp', TYPES.FLOAT32, {
      defaultValue: [0, 0, 0, 0],
      uiHints: [0x02, unit.length, ...unit], // hasUnit only
    })

    const prop = decodeSchemaUpsert(msg, 0)[0] as PropertySchema
    expect(prop.ui.color).toBeNull()
    expect(prop.ui.unit).toBe('\u00B0C')
    expect(prop.ui.icon).toBeNull()
    expect(prop.ui.widget).toBe(0)
  })

  it('decodes no hints', () => {
    const msg = buildPropertySchema(1, 'x', TYPES.UINT8, { uiHints: [0] })
    const prop = decodeSchemaUpsert(msg, 0)[0] as PropertySchema
    expect(prop.ui.color).toBeNull()
    expect(prop.ui.widget).toBe(0)
  })
})

describe('schema validation constraints', () => {
  it('decodes min and max', () => {
    const msg = buildPropertySchema(1, 'val', TYPES.UINT8, {
      validationFlags: 0x03, // hasMin | hasMax
      defaultValue: [0, 255, 100], // min=0, max=255, default=100
      uiHints: [0],
    })
    const prop = decodeSchemaUpsert(msg, 0)[0] as PropertySchema
    expect(prop.constraints.hasMin).toBe(true)
    expect(prop.constraints.min).toBe(0)
    expect(prop.constraints.hasMax).toBe(true)
    expect(prop.constraints.max).toBe(255)
  })

  it('decodes step', () => {
    const msg = buildPropertySchema(1, 'val', TYPES.UINT8, {
      validationFlags: 0x04, // hasStep
      defaultValue: [5, 50], // step=5, default=50
      uiHints: [0],
    })
    const prop = decodeSchemaUpsert(msg, 0)[0] as PropertySchema
    expect(prop.constraints.hasStep).toBe(true)
    expect(prop.constraints.step).toBe(5)
  })
})

describe('function schema', () => {
  function buildFunctionSchema(id: number, name: string, params: Array<{ name: string; typeId: number }>, returnTypeId: number) {
    const nameBytes = enc.encode(name)
    const parts: number[] = [
      0x03, 0x02, 0x04, id, 0,
      nameBytes.length, ...nameBytes,
      0, // description
      params.length,
    ]
    for (const p of params) {
      const pName = enc.encode(p.name)
      parts.push(pName.length, ...pName, p.typeId, 0)
    }
    parts.push(returnTypeId)
    if (returnTypeId !== 0) parts.push(0)
    return new Uint8Array(parts)
  }

  it('decodes function with no params', () => {
    const msg = buildFunctionSchema(0, 'reset', [], 0x00)
    const items = decodeSchemaUpsert(msg, 0)
    const func = items[0] as FunctionSchema
    expect(func.type).toBe('function')
    expect(func.name).toBe('reset')
    expect(func.params.length).toBe(0)
    expect(func.returnTypeId).toBe(0x00)
    expect(func.bleExposed).toBe(true)
  })

  it('decodes function with params and return type', () => {
    const msg = buildFunctionSchema(1, 'save', [{ name: 'slot', typeId: TYPES.UINT8 }], TYPES.BOOL)
    const func = decodeSchemaUpsert(msg, 0)[0] as FunctionSchema
    expect(func.params.length).toBe(1)
    expect(func.params[0].name).toBe('slot')
    expect(func.params[0].typeId).toBe(TYPES.UINT8)
    expect(func.returnTypeId).toBe(TYPES.BOOL)
  })

  it('decodes function with multiple params', () => {
    const msg = buildFunctionSchema(0, 'setPos',
      [{ name: 'x', typeId: TYPES.FLOAT32 }, { name: 'y', typeId: TYPES.FLOAT32 }, { name: 'z', typeId: TYPES.FLOAT32 }],
      TYPES.BOOL,
    )
    const func = decodeSchemaUpsert(msg, 0)[0] as FunctionSchema
    expect(func.params.length).toBe(3)
    expect(func.params[0].name).toBe('x')
    expect(func.params[2].name).toBe('z')
  })

  it('decodes function with description', () => {
    const nameBytes = enc.encode('reset')
    const descBytes = enc.encode('Reset all')
    const msg = new Uint8Array([
      0x03, 0x02, 0x04, 0, 0,
      nameBytes.length, ...nameBytes,
      descBytes.length, ...descBytes,
      0, 0x00,
    ])
    const func = decodeSchemaUpsert(msg, 0)[0] as FunctionSchema
    expect(func.description).toBe('Reset all')
  })
})

describe('decodeDataTypeDefinition', () => {
  it('decodes basic types', () => {
    const data = new Uint8Array([TYPES.UINT8, 0]) // typeId + validation flags (0)
    const result = decodeDataTypeDefinition(data, 0)
    expect(result).not.toBeNull()
    expect(result![0].typeId).toBe(TYPES.UINT8)
  })

  it('decodes ARRAY type', () => {
    const data = new Uint8Array([TYPES.ARRAY, 3, TYPES.UINT8, 0]) // ARRAY, count=3, element=UINT8
    const result = decodeDataTypeDefinition(data, 0)
    expect(result).not.toBeNull()
    expect(result![0].typeId).toBe(TYPES.ARRAY)
    expect(result![0].elementCount).toBe(3)
    expect(result![0].elementTypeDef!.typeId).toBe(TYPES.UINT8)
  })

  it('decodes LIST type', () => {
    const data = new Uint8Array([TYPES.LIST, 0, TYPES.UINT8, 0]) // LIST, length constraints flags=0, element=UINT8
    const result = decodeDataTypeDefinition(data, 0)
    expect(result).not.toBeNull()
    expect(result![0].typeId).toBe(TYPES.LIST)
    expect(result![0].elementTypeDef!.typeId).toBe(TYPES.UINT8)
  })

  it('decodes OBJECT type', () => {
    const xName = enc.encode('x')
    const data = new Uint8Array([
      TYPES.OBJECT, 1, // 1 field
      xName.length, ...xName, TYPES.INT32, 0,
    ])
    const result = decodeDataTypeDefinition(data, 0)
    expect(result![0].fields!.length).toBe(1)
    expect(result![0].fields![0].name).toBe('x')
    expect(result![0].fields![0].typeDef.typeId).toBe(TYPES.INT32)
  })

  it('decodes RESOURCE type', () => {
    const data = new Uint8Array([
      TYPES.RESOURCE,
      TYPES.UINT8, 0, // header type
      TYPES.UINT8, 0, // body type
    ])
    const result = decodeDataTypeDefinition(data, 0)
    expect(result![0].headerTypeDef!.typeId).toBe(TYPES.UINT8)
    expect(result![0].bodyTypeDef!.typeId).toBe(TYPES.UINT8)
  })

  it('decodes STREAM type', () => {
    const data = new Uint8Array([TYPES.STREAM, 100, TYPES.UINT8, 0]) // capacity=100
    const result = decodeDataTypeDefinition(data, 0)
    expect(result![0].historyCapacity).toBe(100)
    expect(result![0].elementTypeDef!.typeId).toBe(TYPES.UINT8)
  })
})
