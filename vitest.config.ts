import { defineConfig } from 'vitest/config'
import path from 'path'

export default defineConfig({
  resolve: {
    alias: {
      '@microproto/client': path.resolve(__dirname, 'module/microproto-client/src/index.ts'),
    },
  },
  test: {
    dir: 'module',
  },
})
