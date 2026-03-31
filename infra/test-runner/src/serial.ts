// Serial monitor - captures device output during integration tests.
// Spawns `pio device monitor` in background, prefixes output lines,
// and kills it when tests finish.

import { spawn, type ChildProcess } from 'child_process'

const DIM = '\x1b[2m'
const YELLOW = '\x1b[33m'
const RESET = '\x1b[0m'
const PREFIX = `${DIM}[device]${RESET} `

export class SerialMonitor {
  private proc: ChildProcess | null = null
  private buffer = ''

  start(baudRate = 115200): void {
    this.proc = spawn('pio', ['device', 'monitor', '--baud', String(baudRate), '--raw'], {
      stdio: ['ignore', 'pipe', 'pipe'],
    })

    this.proc.stdout?.on('data', (chunk: Buffer) => {
      this.buffer += chunk.toString()
      const lines = this.buffer.split('\n')
      this.buffer = lines.pop()! // keep incomplete last line
      for (const line of lines) {
        const trimmed = line.replace(/\r$/, '')
        if (trimmed.length > 0) {
          console.log(`${PREFIX}${YELLOW}${trimmed}${RESET}`)
        }
      }
    })

    this.proc.stderr?.on('data', (chunk: Buffer) => {
      // Suppress pio monitor startup noise, only show actual errors
      const text = chunk.toString().trim()
      if (text && !text.includes('--- Terminal on') && !text.includes('--- Quit:')) {
        console.error(`${PREFIX}${text}`)
      }
    })

    this.proc.on('error', (err) => {
      console.error(`${PREFIX}Serial monitor error: ${err.message}`)
    })
  }

  stop(): void {
    if (this.proc) {
      this.proc.kill('SIGTERM')
      // Force kill after 1s if it doesn't exit
      const forceKill = setTimeout(() => {
        this.proc?.kill('SIGKILL')
      }, 1000)
      this.proc.on('exit', () => clearTimeout(forceKill))
      this.proc = null
    }
    // Flush remaining buffer
    if (this.buffer.trim().length > 0) {
      console.log(`${PREFIX}${YELLOW}${this.buffer.trim()}${RESET}`)
      this.buffer = ''
    }
  }
}
