package io.tunnelchat.internal.proto

import com.google.protobuf.InvalidProtocolBufferException
import io.tunnelchat.api.ProtoArrival
import io.tunnelchat.api.SenderId
import io.tunnelchat.builtin.Echo
import io.tunnelchat.builtin.Pong
import io.tunnelchat.internal.stats.StatsRecorder
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.asSharedFlow

/**
 * Sits between [io.tunnelchat.internal.blob.BlobReceiver] and the public
 * `incomingProtos` / `incomingBlobs` flows. [accept] returns `true` if the payload
 * parsed as a proto envelope — caller MUST NOT surface it on `incomingBlobs` then.
 *
 * Built-ins (`Echo`, `Pong`) are handled internally and NEVER emitted on [arrivals].
 */
internal class ProtoReceiver(
    private val registry: ProtoRegistryImpl,
    /** Auto-reply Pong on receipt of Echo. Supplied by the facade. */
    private val onEcho: suspend (SenderId, Echo) -> Unit,
    /** Resolve outstanding echo probe on receipt of Pong. */
    private val onPong: (SenderId, Pong) -> Unit,
    private val stats: StatsRecorder? = null,
) {
    private val _arrivals = MutableSharedFlow<ProtoArrival>(
        extraBufferCapacity = 32,
        onBufferOverflow = BufferOverflow.DROP_OLDEST,
    )
    val arrivals: SharedFlow<ProtoArrival> = _arrivals.asSharedFlow()

    /**
     * Try to consume [blobBytes] as a proto envelope. Returns `true` if absorbed (built-in
     * auto-reply fired, or an arrival was emitted / dropped as unknown), `false` if the
     * payload is not a proto envelope and should be surfaced as a raw blob.
     */
    suspend fun accept(senderId: SenderId, blobBytes: ByteArray): Boolean {
        val decoded = ProtoEnvelope.tryDecode(blobBytes) ?: return false
        val schemaId = decoded.schemaId
        val payload = decoded.payload
        stats?.incProtoIn()
        when (schemaId.toInt()) {
            BuiltinSchemas.ECHO -> {
                val echo = try { Echo.parseFrom(payload) } catch (_: InvalidProtocolBufferException) { return true }
                onEcho(senderId, echo)
                return true
            }
            BuiltinSchemas.PONG -> {
                val pong = try { Pong.parseFrom(payload) } catch (_: InvalidProtocolBufferException) { return true }
                onPong(senderId, pong)
                return true
            }
        }
        val parser = registry.parserFor(schemaId)
        if (parser == null) {
            stats?.incProtoUnknownSchema()
            _arrivals.tryEmit(ProtoArrival.Unknown(senderId, schemaId, payload))
            return true
        }
        val message = try {
            parser.parseFrom(payload)
        } catch (_: InvalidProtocolBufferException) {
            stats?.incProtoUnknownSchema()
            _arrivals.tryEmit(ProtoArrival.Unknown(senderId, schemaId, payload))
            return true
        }
        _arrivals.tryEmit(ProtoArrival.Known(senderId, schemaId, message))
        return true
    }
}
