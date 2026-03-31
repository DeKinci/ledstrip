import path from 'path'
import { fileURLToPath } from 'url'

// Project root: 3 levels up from infra/build/src/
export const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..')
