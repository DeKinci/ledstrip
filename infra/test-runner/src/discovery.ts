import { execSync } from 'child_process'

const BOLD = '\x1b[1m'
const GREEN = '\x1b[32m'
const YELLOW = '\x1b[33m'
const RED = '\x1b[31m'
const DIM = '\x1b[2m'
const RESET = '\x1b[0m'

/** Well-known Espressif MAC prefixes */
const ESPRESSIF_PREFIXES = new Set([
  '08:3a:f2', '08:b6:1f', '08:d1:f9', '10:20:ba', '10:52:1c', '10:97:bd',
  '24:0a:c4', '24:62:ab', '24:6f:28', '30:ae:a4', '34:85:18', '34:86:5d',
  '34:94:54', '34:ab:95', '3c:71:bf', '40:4c:ca', '40:f5:20', '48:27:e2',
  '48:3f:da', '48:ca:43', '48:e7:29', '54:43:b2', '58:bf:25', '5c:cf:7f',
  '60:01:94', '60:55:f9', '64:b7:08', '68:67:25', '68:c6:3a', '70:03:9f',
  '70:b8:f6', '74:4d:bd', '78:21:84', '78:e3:6d', '7c:9e:bd', '7c:df:a1',
  '80:7d:3a', '84:0d:8e', '84:cc:a8', '84:f3:eb', '8c:aa:b5', '8c:ce:4e',
  '94:3c:c6', '94:b5:55', '94:b9:7e', '98:cd:ac', 'a0:20:a6', 'a0:76:4e',
  'a4:cf:12', 'a4:e5:7c', 'a8:03:2a', 'a8:42:e3', 'ac:0b:fb', 'ac:67:b2',
  'b0:a7:32', 'b4:e6:2d', 'b8:d6:1a', 'b8:f8:62', 'bc:dd:c2', 'c0:49:ef',
  'c4:4f:33', 'c4:5b:be', 'c4:de:e2', 'c8:2b:96', 'c8:f0:9e', 'cc:50:e3',
  'd4:8a:fc', 'd4:f9:8d', 'd8:13:2a', 'd8:bf:c0', 'dc:4f:22', 'dc:54:75',
  'e0:5a:1b', 'e0:98:06', 'e8:31:cd', 'e8:68:e7', 'e8:6b:ea', 'e8:9f:6d',
  'ec:62:60', 'ec:64:c9', 'ec:da:3b', 'ec:fa:bc', 'f0:08:d1', 'f0:9e:9e',
  'f4:12:fa', 'f4:65:a6', 'f4:cf:a2', 'fc:f5:c4',
])

function findEsp32InArp(): string | null {
  try {
    const output = execSync('arp -a', { encoding: 'utf-8', timeout: 5000 })
    const pattern = /\((\d+\.\d+\.\d+\.\d+)\)\s+at\s+([0-9a-fA-F:]+)/g
    let match
    while ((match = pattern.exec(output)) !== null) {
      const ip = match[1]
      const mac = match[2].toLowerCase()
      const prefix = mac.split(':').slice(0, 3).join(':')
      if (ESPRESSIF_PREFIXES.has(prefix)) {
        return ip
      }
    }
  } catch {}
  return null
}

function broadcastPing(): void {
  try {
    const output = execSync('ifconfig', { encoding: 'utf-8', timeout: 5000 })
    const match = /inet\s+(\d+\.\d+\.\d+)\.\d+/.exec(output)
    if (match) {
      const subnet = match[1]
      try {
        execSync(`ping -c 1 -t 1 ${subnet}.255`, { timeout: 3000, stdio: 'ignore' })
      } catch {}
    }
  } catch {}
}

/**
 * Discover an ESP32 device on the local network.
 * Checks DEVICE_IP env var first, then ARP table, then broadcast ping + ARP.
 */
export async function discoverDevice(): Promise<string | null> {
  // 1. Environment variable
  const envIp = process.env.DEVICE_IP
  if (envIp) {
    console.log(`${GREEN}Using DEVICE_IP:${RESET} ${envIp}`)
    return envIp
  }

  console.log(`${DIM}Discovering ESP32 device...${RESET}`)

  // 2. Check ARP table
  const ip = findEsp32InArp()
  if (ip) {
    console.log(`${GREEN}Found ESP32:${RESET} ${ip}`)
    return ip
  }

  // 3. Broadcast ping to populate ARP, then retry
  console.log(`${YELLOW}Not in ARP cache, pinging subnet...${RESET}`)
  broadcastPing()
  await new Promise(r => setTimeout(r, 500))

  const ip2 = findEsp32InArp()
  if (ip2) {
    console.log(`${GREEN}Found ESP32:${RESET} ${ip2}`)
    return ip2
  }

  console.log(`${RED}No ESP32 device found${RESET}`)
  return null
}

/**
 * Poll for device to come online after firmware upload.
 * Retries ARP discovery every second for up to timeoutMs.
 */
export async function waitForDevice(timeoutMs = 30000): Promise<string | null> {
  const start = Date.now()
  console.log(`${DIM}Waiting for device to come online...${RESET}`)

  while (Date.now() - start < timeoutMs) {
    const ip = findEsp32InArp()
    if (ip) {
      // Verify it's actually reachable with a quick HTTP ping
      try {
        const res = await fetch(`http://${ip}/ping`, { signal: AbortSignal.timeout(2000) })
        if (res.ok) {
          console.log(`${GREEN}Device online:${RESET} ${ip}`)
          return ip
        }
      } catch {}
    }
    await new Promise(r => setTimeout(r, 1000))
  }

  return null
}
