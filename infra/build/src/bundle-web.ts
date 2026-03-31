#!/usr/bin/env node
// Build web resource bundles from the monorepo.
//
// web/iife-entry.ts  --> rsc_gen/microproto-ui.js
// module/microproto-client/src/index.ts --> rsc_gen/microproto-client.js
// Both copied to service/gateway/internal/web/static/

import * as esbuild from 'esbuild'
import * as path from 'path'
import * as fs from 'fs'
import { ROOT } from './paths.js'

const rscGen = path.join(ROOT, 'lib/microproto-web/rsc_gen')
const gatewayStatic = path.join(ROOT, 'service/gateway/internal/web/static')

fs.mkdirSync(rscGen, { recursive: true })

// 1. Client IIFE
console.log('Building @microproto/client IIFE...')
await esbuild.build({
  entryPoints: [path.join(ROOT, 'module/microproto-client/src/index.ts')],
  bundle: true,
  minify: true,
  format: 'iife',
  globalName: 'MicroProtoClientModule',
  target: 'es2020',
  outfile: path.join(rscGen, 'microproto-client.js'),
  footer: { js: 'var MicroProtoClient=MicroProtoClientModule.MicroProtoClient;' },
})
console.log(`  → microproto-client.js (${fs.statSync(path.join(rscGen, 'microproto-client.js')).size} bytes)`)

// 2. Widget IIFE
console.log('Building @microproto/widgets IIFE...')
await esbuild.build({
  entryPoints: [path.join(ROOT, 'lib/microproto-web/web/iife-entry.ts')],
  bundle: true,
  minify: true,
  format: 'iife',
  target: 'es2020',
  outfile: path.join(rscGen, 'microproto-ui.js'),
})
console.log(`  → microproto-ui.js (${fs.statSync(path.join(rscGen, 'microproto-ui.js')).size} bytes)`)

// 3. Copy to gateway static
console.log('Copying to gateway static...')
fs.mkdirSync(gatewayStatic, { recursive: true })
fs.copyFileSync(path.join(rscGen, 'microproto-client.js'), path.join(gatewayStatic, 'microproto-client.js'))
fs.copyFileSync(path.join(rscGen, 'microproto-ui.js'), path.join(gatewayStatic, 'microproto-ui.js'))

console.log('Done.')
