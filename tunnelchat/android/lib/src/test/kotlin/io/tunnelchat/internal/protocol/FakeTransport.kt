package io.tunnelchat.internal.protocol

import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.receiveAsFlow

/** In-process fake of [CommandTransport] for dispatcher tests. */
internal class FakeTransport(
    /** When false, [send] returns false to simulate a disconnected link. */
    var acceptSends: Boolean = true,
) : CommandTransport {
    private val inboundChan = Channel<ByteArray>(Channel.UNLIMITED)
    override val inbound: Flow<ByteArray> = inboundChan.receiveAsFlow()

    val sent = mutableListOf<ByteArray>()

    override fun send(frame: ByteArray): Boolean {
        if (!acceptSends) return false
        sent += frame
        return true
    }

    /** Push an inbound frame as if the device sent a notification. */
    fun deliver(frame: ByteArray) { inboundChan.trySend(frame) }

    fun lastSentCmd(): Byte = sent.last()[0]
}
