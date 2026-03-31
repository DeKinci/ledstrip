import { defineConfig } from 'vitest/config'
import path from 'path'

export default defineConfig({
  resolve: {
    alias: {
      '@microproto/client': path.resolve(__dirname, 'module/microproto-client/src/index.ts'),
      '@test': path.resolve(__dirname, 'infra/test-runner/src'),
    },
  },
  test: {
    include: ['**/*.test.ts'],
    exclude: ['**/node_modules/**', '**/dist/**'],
    globalSetup: './infra/test-runner/src/global-setup.ts',
    testTimeout: parseInt(process.env.TEST_TIMEOUT || '10000', 10),
    // Integration tests must run sequentially (one device)
    pool: 'forks',
    poolOptions: { forks: { singleFork: true } },
  },
})