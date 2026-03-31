import { defineConfig } from 'vitest/config'
import path from 'path'
import { fileURLToPath } from 'url'

const __dirname = path.dirname(fileURLToPath(import.meta.url))

const testTimeout = parseInt(process.env.TEST_TIMEOUT || '10000', 10)

export default defineConfig({
  resolve: {
    alias: {
      '@test': path.resolve(__dirname, 'src'),
    },
  },
  test: {
    testTimeout,
    hookTimeout: 15000,
    sequence: { concurrent: false },
    globalSetup: path.resolve(__dirname, 'src/global-setup.ts'),
  },
})
