package io.tunnelchat.internal.proto

import com.google.protobuf.MessageLite
import io.tunnelchat.api.BlobHandle
import io.tunnelchat.internal.blob.BlobSender
import io.tunnelchat.internal.stats.StatsRecorder

/**
 * Serialises a protobuf message, wraps it with a [ProtoEnvelope] header, and hands
 * the resulting bytes to [BlobSender]. Does not know about registries — receivers do
 * the dispatch on the other side.
 */
internal class ProtoSender(
    private val blobSender: BlobSender,
    private val stats: StatsRecorder? = null,
) {
    fun send(schemaId: UShort, message: MessageLite): BlobHandle {
        val framed = ProtoEnvelope.encode(schemaId, message.toByteArray())
        val handle = blobSender.enqueue(framed)
        stats?.incProtoOut()
        return handle
    }
}
