import { MicroProtoClient } from '@microproto/client'
import { register, renderAll, Slider, Toggle, ErrorDisplay, CodeEditor, LedCanvas, SegmentEditor, HueSlider } from '@microproto/widgets'

register(Slider, Toggle, ErrorDisplay, CodeEditor, LedCanvas, SegmentEditor, HueSlider)

let currentClient: MicroProtoClient | null = null
let currentDestroy: (() => void) | null = null

const wsProto = location.protocol === 'https:' ? 'wss:' : 'ws:'

function connectToDevice(deviceId: string) {
  // Disconnect previous
  if (currentClient) {
    currentClient.disconnect()
    currentDestroy?.()
  }

  const wsUrl = import.meta.env.DEV
    ? `${wsProto}//${location.host}/ws/proxy/${deviceId}`
    : `${wsProto}//${location.host}/ws/proxy/${deviceId}`

  const mainEl = document.getElementById('main')!
  mainEl.innerHTML = ''

  const uiDiv = document.createElement('div')
  mainEl.appendChild(uiDiv)

  currentClient = new MicroProtoClient(wsUrl, { debug: false })

  const { destroy } = renderAll(uiDiv, currentClient, { layout: 'single' })
  currentDestroy = destroy

  currentClient.on('error', (err: any) => {
    const el = document.createElement('div')
    el.className = 'toast toast-error'
    el.textContent = err.message || String(err)
    document.getElementById('toasts')!.appendChild(el)
    setTimeout(() => el.remove(), 4000)
  })

  currentClient.connect()
}

// Login
const loginBtn = document.getElementById('loginBtn')!
const loginToken = document.getElementById('loginToken') as HTMLInputElement
const loginOverlay = document.getElementById('loginOverlay')!
const loginError = document.getElementById('loginError')!

// Check for saved token
const savedToken = localStorage.getItem('gateway_token')
if (savedToken) {
  loginOverlay.classList.add('hidden')
  loadDevices(savedToken)
}

loginBtn.addEventListener('click', async () => {
  const token = loginToken.value.trim()
  if (!token) return

  try {
    const res = await fetch('/api/devices', {
      headers: { Authorization: `Bearer ${token}` },
    })
    if (!res.ok) throw new Error('Invalid token')

    localStorage.setItem('gateway_token', token)
    loginOverlay.classList.add('hidden')
    loadDevices(token)
  } catch (e: any) {
    loginError.textContent = e.message
    loginError.classList.remove('hidden')
  }
})

async function loadDevices(token: string) {
  try {
    const res = await fetch('/api/devices', {
      headers: { Authorization: `Bearer ${token}` },
    })
    if (!res.ok) throw new Error('Failed to load devices')

    const devices: Array<{ id: string; name: string; online: boolean }> = await res.json()
    const list = document.getElementById('deviceList')!
    list.innerHTML = ''

    for (const device of devices) {
      const el = document.createElement('div')
      el.className = 'device-item'
      el.innerHTML = `
        <div class="device-dot ${device.online ? 'online' : 'offline'}"></div>
        <div>
          <div class="device-name">${device.name || device.id}</div>
          <div class="device-id">${device.id}</div>
        </div>
      `
      el.addEventListener('click', () => {
        list.querySelectorAll('.device-item').forEach(i => i.classList.remove('active'))
        el.classList.add('active')
        connectToDevice(device.id)
      })
      list.appendChild(el)
    }
  } catch (e: any) {
    console.error('Failed to load devices:', e)
  }
}
