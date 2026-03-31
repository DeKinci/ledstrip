#!/usr/bin/env node

import WebSocket from 'ws'
import { MicroProtoClient, MemoryStorage } from '@microproto/client'
import { Formatter } from './formatter.js'

const RESET = '\x1b[0m'
const BOLD = '\x1b[1m'
const DIM = '\x1b[2m'
const CYAN = '\x1b[36m'
const RED = '\x1b[31m'
const GREEN = '\x1b[32m'

function usage() {
  console.log(`
${BOLD}microproto${RESET} - MicroProto protocol debugger

${BOLD}Usage:${RESET}
  microproto <url> [options]

${BOLD}Examples:${RESET}
  microproto ws://192.168.1.100:81          ${DIM}# Connect to device${RESET}
  microproto ws://localhost:8080/ws/proxy/1  ${DIM}# Connect via gateway${RESET}
  microproto ws://192.168.1.100:81 --raw    ${DIM}# Show raw hex bytes${RESET}

${BOLD}Options:${RESET}
  --raw       Show raw hex bytes alongside decoded output
  --hex-only  Show only raw hex bytes (no decoding)
  --help      Show this help
`)
}

const args = process.argv.slice(2)
const flags = new Set(args.filter(a => a.startsWith('--')))
const positional = args.filter(a => !a.startsWith('--'))

if (flags.has('--help') || positional.length === 0) {
  usage()
  process.exit(positional.length === 0 ? 1 : 0)
}

const url = positional[0]
const showRaw = flags.has('--raw')
const hexOnly = flags.has('--hex-only')

function hexDump(data: Uint8Array): string {
  return Array.from(data).map(b => b.toString(16).padStart(2, '0')).join(' ')
}

const formatter = new Formatter()

console.log(`${CYAN}${BOLD}microproto${RESET} connecting to ${BOLD}${url}${RESET}...`)

// Create a raw WebSocket to intercept all messages
const ws = new WebSocket(url)
ws.binaryType = 'arraybuffer'

// Also create a MicroProtoClient using the same connection to maintain state
const client = new MicroProtoClient(url, {
  reconnect: true,
  reconnectDelay: 3000,
  debug: false,
  storage: new MemoryStorage(),
  webSocketFactory: () => ws as any,
})

// Intercept outgoing messages by monkey-patching send
const originalSend = ws.send.bind(ws)
ws.send = function (data: any, ...rest: any[]) {
  const bytes = data instanceof ArrayBuffer
    ? new Uint8Array(data)
    : data instanceof Uint8Array ? data : new Uint8Array(data)

  if (hexOnly) {
    console.log(`>>> ${hexDump(bytes)}`)
  } else {
    const line = formatter.formatSent(bytes)
    console.log(line)
    if (showRaw) console.log(`    ${DIM}${hexDump(bytes)}${RESET}`)
  }

  return originalSend(data, ...rest)
} as any

ws.on('open', () => {
  console.log(`${GREEN}${BOLD}connected${RESET}`)
  // Trigger client's onopen handler
  if ((ws as any).onopen) (ws as any).onopen({})
})

ws.on('message', (data: ArrayBuffer | Buffer) => {
  const bytes = new Uint8Array(data instanceof ArrayBuffer ? data : data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength))

  if (hexOnly) {
    console.log(`<<< ${hexDump(bytes)}`)
  } else {
    const line = formatter.formatReceived(bytes)
    console.log(line)
    if (showRaw) console.log(`    ${DIM}${hexDump(bytes)}${RESET}`)
  }

  // Forward to client for state tracking
  if ((ws as any).onmessage) {
    (ws as any).onmessage({ data: bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength) })
  }
})

ws.on('close', () => {
  console.log(`${RED}${BOLD}disconnected${RESET}`)
})

ws.on('error', (err: Error) => {
  console.error(`${RED}error: ${err.message}${RESET}`)
})

// Handle Ctrl+C
process.on('SIGINT', () => {
  console.log(`\n${DIM}closing...${RESET}`)
  ws.close()
  process.exit(0)
})
