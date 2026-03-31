import fs from 'fs'
import path from 'path'
import { execSync } from 'child_process'
import { buildAndUpload } from './firmware.js'
import { discoverDevice, waitForDevice } from './discovery.js'
import { createDeviceContext, type SuiteManifest, type DeviceContext } from './context.js'

const BOLD = '\x1b[1m'
const GREEN = '\x1b[32m'
const RED = '\x1b[31m'
const CYAN = '\x1b[36m'
const DIM = '\x1b[2m'
const RESET = '\x1b[0m'

export interface RunOptions {
  ip?: string
  skipFlash?: boolean
  buildOnly?: boolean
}

/**
 * Run a test suite end-to-end:
 * 1. Read suite.json
 * 2. Build & upload firmware (if specified)
 * 3. Discover device
 * 4. Run vitest with suite's test dir
 * 5. Report results
 */
export async function runSuite(projectDir: string, suitePath: string, opts: RunOptions = {}): Promise<boolean> {
  const manifestPath = path.join(suitePath, 'suite.json')

  // 1. Read manifest
  if (!fs.existsSync(manifestPath)) {
    console.error(`${RED}No suite.json found at ${manifestPath}${RESET}`)
    return false
  }

  const manifest: SuiteManifest = JSON.parse(fs.readFileSync(manifestPath, 'utf-8'))
  console.log(`\n${CYAN}${BOLD}━━━ Suite: ${manifest.name} ━━━${RESET}`)
  console.log(`${DIM}Firmware: ${manifest.firmware || 'none'} | Capabilities: ${manifest.capabilities.join(', ')}${RESET}\n`)

  // 2. Build & upload firmware
  if (manifest.firmware && !opts.skipFlash) {
    const ok = buildAndUpload(manifest.firmware, projectDir)
    if (!ok) return false

    // Wait for device to reboot
    console.log(`${DIM}Waiting for device reboot...${RESET}`)
    await new Promise(r => setTimeout(r, 3000))
  }

  // 3. Discover device
  let ip = opts.ip || process.env.DEVICE_IP || null

  if (!ip) {
    if (manifest.firmware && !opts.skipFlash) {
      // Just flashed — poll for it to come back
      ip = await waitForDevice(30000)
    } else {
      ip = await discoverDevice()
    }
  }

  if (!ip) {
    console.error(`${RED}Could not find device${RESET}`)
    return false
  }

  // 4. Run vitest
  const testsDir = path.join(suitePath, 'tests')
  if (!fs.existsSync(testsDir)) {
    console.error(`${RED}No tests/ directory in ${suitePath}${RESET}`)
    return false
  }

  console.log(`\n${BOLD}Running tests...${RESET}\n`)

  try {
    execSync(
      `npx vitest run --dir "${testsDir}" --config "${path.join(path.dirname(new URL(import.meta.url).pathname), '../vitest.suite.ts')}"`,
      {
        cwd: projectDir,
        stdio: 'inherit',
        timeout: (manifest.timeout || 60000) + 10000,
        env: {
          ...process.env,
          DEVICE_IP: ip,
          SUITE_PATH: suitePath,
        },
      },
    )
    console.log(`\n${GREEN}${BOLD}━━━ ${manifest.name}: PASSED ━━━${RESET}\n`)
    return true
  } catch (e) {
    console.log(`\n${RED}${BOLD}━━━ ${manifest.name}: FAILED ━━━${RESET}\n`)
    return false
  }
}

/** List all available test suites by scanning lib and device dirs for test/integration suites */
export function listSuites(projectDir: string): Array<{ path: string; manifest: SuiteManifest }> {
  const suites: Array<{ path: string; manifest: SuiteManifest }> = []

  function scanDir(baseDir: string) {
    if (!fs.existsSync(baseDir)) return
    for (const owner of fs.readdirSync(baseDir)) {
      const integrationDir = path.join(baseDir, owner, 'test', 'integration')
      if (!fs.existsSync(integrationDir)) continue

      for (const suite of fs.readdirSync(integrationDir)) {
        const suitePath = path.join(integrationDir, suite)
        const manifestPath = path.join(suitePath, 'suite.json')
        if (fs.existsSync(manifestPath)) {
          const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf-8'))
          suites.push({ path: suitePath, manifest })
        }
      }
    }
  }

  scanDir(path.join(projectDir, 'lib'))
  scanDir(path.join(projectDir, 'device'))

  return suites
}
