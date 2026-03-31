#!/usr/bin/env node

import path from 'path'
import fs from 'fs'
import { runSuite, listSuites } from './orchestrator.js'

const BOLD = '\x1b[1m'
const DIM = '\x1b[2m'
const CYAN = '\x1b[36m'
const GREEN = '\x1b[32m'
const YELLOW = '\x1b[33m'
const RED = '\x1b[31m'
const RESET = '\x1b[0m'

function usage() {
  console.log(`
${BOLD}test-device${RESET} - ESP32 integration test runner

${BOLD}Usage:${RESET}
  test-device <suite>              Run a test suite
  test-device list                 List available suites
  test-device all                  Run all suites sequentially

${BOLD}Options:${RESET}
  --ip <addr>       Device IP (skip discovery)
  --skip-flash      Don't build/upload firmware
  --help            Show this help

${BOLD}Examples:${RESET}
  test-device microproto-basic
  test-device gaslamp --ip 192.168.1.50
  test-device all --skip-flash
`)
}

const args = process.argv.slice(2)
const flags = new Set<string>()
const positional: string[] = []
let ipOverride: string | undefined

for (let i = 0; i < args.length; i++) {
  if (args[i] === '--ip' && args[i + 1]) {
    ipOverride = args[++i]
  } else if (args[i].startsWith('--')) {
    flags.add(args[i])
  } else {
    positional.push(args[i])
  }
}

if (flags.has('--help') || positional.length === 0) {
  usage()
  process.exit(positional.length === 0 ? 1 : 0)
}

// Project root: walk up until we find platformio.ini
function findProjectRoot(): string {
  let dir = path.dirname(new URL(import.meta.url).pathname)
  while (dir !== path.dirname(dir)) {
    if (fs.existsSync(path.join(dir, 'platformio.ini'))) return dir
    dir = path.dirname(dir)
  }
  throw new Error('Could not find project root (no platformio.ini found)')
}
const projectDir = findProjectRoot()
const command = positional[0]

if (command === 'list') {
  const suites = listSuites(projectDir)
  if (suites.length === 0) {
    console.log(`${YELLOW}No test suites found${RESET}`)
  } else {
    console.log(`\n${BOLD}Available test suites:${RESET}\n`)
    for (const { manifest } of suites) {
      const caps = manifest.capabilities.join(', ')
      const fw = manifest.firmware || 'none'
      console.log(`  ${CYAN}${manifest.name}${RESET}  ${DIM}firmware=${fw} caps=[${caps}]${RESET}`)
    }
    console.log()
  }
  process.exit(0)
}

const opts = {
  ip: ipOverride,
  skipFlash: flags.has('--skip-flash'),
}

async function main() {
  if (command === 'all') {
    const suites = listSuites(projectDir)
    let passed = 0
    let failed = 0

    for (const { path: suitePath, manifest } of suites) {
      const ok = await runSuite(projectDir, suitePath, opts)
      if (ok) passed++
      else failed++
    }

    console.log(`\n${BOLD}Results: ${GREEN}${passed} passed${RESET}, ${failed > 0 ? RED : ''}${failed} failed${RESET}\n`)
    process.exit(failed > 0 ? 1 : 0)
  }

  // Find suite by name
  const suites = listSuites(projectDir)
  const suite = suites.find(s => s.manifest.name === command)

  if (!suite) {
    // Try as a path
    const directPath = path.resolve(command)
    if (fs.existsSync(path.join(directPath, 'suite.json'))) {
      const ok = await runSuite(projectDir, directPath, opts)
      process.exit(ok ? 0 : 1)
    }

    console.error(`${RED}Unknown suite: ${command}${RESET}`)
    console.error(`Run ${BOLD}test-device list${RESET} to see available suites`)
    process.exit(1)
  }

  const ok = await runSuite(projectDir, suite.path, opts)
  process.exit(ok ? 0 : 1)
}

main().catch(e => {
  console.error(`${RED}Fatal: ${e.message}${RESET}`)
  process.exit(1)
})
