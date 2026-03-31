/** Protocol version */
export const PROTOCOL_VERSION = 1

/** Opcodes (bits 0-3 of header byte) */
export const OPCODES = {
  HELLO: 0x00,
  PROPERTY_UPDATE: 0x01,
  // 0x02 reserved for PROPERTY_DELTA
  SCHEMA_UPSERT: 0x03,
  SCHEMA_DELETE: 0x04,
  RPC: 0x05,
  PING: 0x06,
  ERROR: 0x07,
  RESOURCE_GET: 0x08,
  RESOURCE_PUT: 0x09,
  RESOURCE_DELETE: 0x0a,
} as const

/** Type IDs */
export const TYPES = {
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
  STREAM: 0x25,
} as const

/** Error codes */
export const ERROR_CODES = {
  SUCCESS: 0x0000,
  INVALID_OPCODE: 0x0001,
  INVALID_PROPERTY_ID: 0x0002,
  INVALID_FUNCTION_ID: 0x0003,
  TYPE_MISMATCH: 0x0004,
  VALIDATION_FAILED: 0x0005,
  OUT_OF_RANGE: 0x0006,
  PERMISSION_DENIED: 0x0007,
  NOT_IMPLEMENTED: 0x0008,
  PROTOCOL_VERSION_MISMATCH: 0x0009,
  BUFFER_OVERFLOW: 0x000a,
} as const

/** Widget hints */
export const WIDGETS = {
  AUTO: 0,
  SLIDER: 1,
  TOGGLE: 2,
  COLOR_PICKER: 3,
  TEXT_INPUT: 4,
  DROPDOWN: 5,
  NUMBER_INPUT: 6,
} as const

/** Pastel color palette (index → hex) */
export const COLORS: Record<number, string | null> = {
  0: null,
  1: '#fda4af',  // ROSE
  2: '#fcd34d',  // AMBER
  3: '#bef264',  // LIME
  4: '#67e8f9',  // CYAN
  5: '#c4b5fd',  // VIOLET
  6: '#f9a8d4',  // PINK
  7: '#5eead4',  // TEAL
  8: '#fdba74',  // ORANGE
  9: '#7dd3fc',  // SKY
  10: '#a5b4fc', // INDIGO
  11: '#6ee7b7', // EMERALD
  12: '#cbd5e1', // SLATE
}

/** Color index → name */
export const COLOR_NAMES: Record<number, string | null> = {
  0: null, 1: 'rose', 2: 'amber', 3: 'lime', 4: 'cyan', 5: 'violet',
  6: 'pink', 7: 'teal', 8: 'orange', 9: 'sky', 10: 'indigo', 11: 'emerald', 12: 'slate',
}
