package io.tunnelchat.internal.protocol

import kotlinx.coroutines.flow.Flow

/**
 * Abstraction over the raw BLE link that [CommandDispatcher] rides on. `BleSession`
 * implements this; unit tests pass a fake.
 *
 * Contract:
 * - [inbound] emits each NUS-TX payload exactly once, in arrival order.
 * - [send] is non-blocking; returns `false` when the link is not [io.tunnelchat.api.ConnectionState.Connected]
 *   or the write could not be queued.
 */
internal interface CommandTransport {
    val inbound: Flow<ByteArray>
    fun send(frame: ByteArray): Boolean
}
