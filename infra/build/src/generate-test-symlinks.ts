#!/usr/bin/env node
// Auto-generates test_generated/ with symlinks into lib and device test dirs.
// PlatformIO uses test_generated/ as its test_dir.
//
// Run: npx tsx infra/build/src/generate-test-symlinks.ts
// Called automatically by the PlatformIO pre-build hook.

import fs from 'fs'
import path from 'path'
import { ROOT } from './paths.js'
const OUT = path.join(ROOT, 'test_generated')

function symlink(target: string, link: string) {
  const rel = path.relative(path.dirname(link), target)
  if (fs.existsSync(link)) fs.rmSync(link)
  fs.symlinkSync(rel, link)
}

function scanTestDirs(baseDir: string, categories: string[]) {
  const results: Array<{ category: string; name: string; target: string }> = []

  if (!fs.existsSync(baseDir)) return results

  for (const owner of fs.readdirSync(baseDir)) {
    for (const category of categories) {
      const catDir = path.join(baseDir, owner, 'test', category)
      if (!fs.existsSync(catDir)) continue

      for (const testDir of fs.readdirSync(catDir)) {
        const fullPath = path.join(catDir, testDir)
        if (fs.statSync(fullPath).isDirectory()) {
          results.push({ category, name: testDir, target: fullPath })
        }
      }
    }
  }

  return results
}

// Clean and recreate
if (fs.existsSync(OUT)) fs.rmSync(OUT, { recursive: true })
fs.mkdirSync(OUT, { recursive: true })

// Scan lib/ for native and onboard tests
const libTests = scanTestDirs(path.join(ROOT, 'lib'), ['native', 'onboard'])

// Scan device/ for onboard tests
const deviceTests = scanTestDirs(path.join(ROOT, 'device'), ['onboard'])

const allTests = [...libTests, ...deviceTests]

// Create category dirs and symlinks
const categories = new Set(allTests.map(t => t.category))
for (const cat of categories) {
  fs.mkdirSync(path.join(OUT, cat), { recursive: true })
}

let count = 0
for (const { category, name, target } of allTests) {
  const link = path.join(OUT, category, name)
  symlink(target, link)
  count++
}

console.log(`Generated ${count} test symlinks in test_generated/`)
