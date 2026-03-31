import { defineConfig } from 'vitest/config'
import path from 'path'
import { fileURLToPath } from 'url'

const __dirname = path.dirname(fileURLToPath(import.meta.url))

export default defineConfig({
  resolve: {
    alias: {
      '@test': path.resolve(__dirname, 'src'),
    },
  },
  test: {
    testTimeout: 30000,
    hookTimeout: 15000,
    sequence: { concurrent: false },
    globalSetup: './src/global-setup.ts',
  },
})
