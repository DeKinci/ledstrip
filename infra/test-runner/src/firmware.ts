import { execSync } from 'child_process'
import path from 'path'

const BOLD = '\x1b[1m'
const GREEN = '\x1b[32m'
const RED = '\x1b[31m'
const DIM = '\x1b[2m'
const RESET = '\x1b[0m'

/**
 * Build and upload firmware for a PlatformIO environment.
 * Returns true on success.
 */
export function buildAndUpload(env: string, projectDir: string): boolean {
  console.log(`${BOLD}Building firmware:${RESET} ${env}`)

  try {
    execSync(`pio run -e ${env}`, {
      cwd: projectDir,
      stdio: 'inherit',
      timeout: 120000,
    })
  } catch (e) {
    console.error(`${RED}Build failed for ${env}${RESET}`)
    return false
  }

  console.log(`${BOLD}Uploading firmware:${RESET} ${env}`)

  try {
    execSync(`pio run -e ${env} -t upload`, {
      cwd: projectDir,
      stdio: 'inherit',
      timeout: 60000,
    })
  } catch (e) {
    console.error(`${RED}Upload failed for ${env}${RESET}`)
    return false
  }

  console.log(`${GREEN}Firmware uploaded successfully${RESET}`)
  return true
}

/**
 * Build firmware only (no upload). For CI dry-run validation.
 */
export function buildOnly(env: string, projectDir: string): boolean {
  console.log(`${BOLD}Building firmware:${RESET} ${env}`)

  try {
    execSync(`pio run -e ${env}`, {
      cwd: projectDir,
      stdio: 'inherit',
      timeout: 120000,
    })
    console.log(`${GREEN}Build successful${RESET}`)
    return true
  } catch (e) {
    console.error(`${RED}Build failed for ${env}${RESET}`)
    return false
  }
}
