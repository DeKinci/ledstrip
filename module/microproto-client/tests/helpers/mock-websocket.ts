import type { WebSocketLike } from '@microproto/client'

export class MockWebSocket implements WebSocketLike {
  static CONNECTING = 0
  static OPEN = 1
  static CLOSING = 2
  static CLOSED = 3

  readyState = MockWebSocket.CONNECTING
  binaryType = 'arraybuffer'
  sentMessages: ArrayBuffer[] = []
  onopen: ((ev: any) => void) | null = null
  onclose: ((ev: any) => void) | null = null
  onmessage: ((ev: any) => void) | null = null
  onerror: ((ev: any) => void) | null = null

  constructor(public url: string) {}

  send(data: ArrayBuffer | Uint8Array): void {
    if (data instanceof Uint8Array) {
      this.sentMessages.push(data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength))
    } else {
      this.sentMessages.push(data)
    }
  }

  close(): void {
    this.readyState = MockWebSocket.CLOSED
    if (this.onclose) this.onclose({})
  }

  // Test helpers
  simulateOpen(): void {
    this.readyState = MockWebSocket.OPEN
    if (this.onopen) this.onopen({})
  }

  simulateMessage(data: Uint8Array): void {
    if (this.onmessage) {
      this.onmessage({ data: data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength) })
    }
  }

  simulateClose(): void {
    this.readyState = MockWebSocket.CLOSED
    if (this.onclose) this.onclose({})
  }

  getSentBytes(index: number): Uint8Array {
    return new Uint8Array(this.sentMessages[index])
  }
}
