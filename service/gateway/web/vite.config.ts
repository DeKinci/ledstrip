import { defineConfig } from 'vite'

export default defineConfig({
  server: {
    proxy: {
      '/ws': {
        target: `ws://${process.env.GATEWAY_URL || 'localhost:8080'}`,
        ws: true,
      },
      '/api': {
        target: `http://${process.env.GATEWAY_URL || 'localhost:8080'}`,
      },
    },
  },
  build: {
    rollupOptions: {
      output: {
        format: 'iife' as const,
        entryFileNames: 'microproto-ui.js',
        assetFileNames: '[name][extname]',
      },
    },
    minify: 'esbuild',
    outDir: 'dist',
    cssCodeSplit: false,
  },
})
