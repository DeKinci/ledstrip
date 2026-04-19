package io.tunnelchat.internal.blob

import io.tunnelchat.api.BlobArrival
import io.tunnelchat.api.BlobId
import io.tunnelchat.api.BlobReceiveProgress
import io.tunnelchat.api.SenderId
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import java.security.MessageDigest

/**
 * Reassembles inbound blob chunks. Designed to sit between [io.tunnelchat.internal.protocol.CommandDispatcher]'s
 * `incomingMessages` and the public `incomingMessages` flow: [accept] returns `true` if
 * the payload was absorbed as a blob chunk (do NOT re-emit as plain text), `false` if it
 * was not a blob.
 *
 * Receive-local invariants:
 * - Out-of-order + duplicate chunks are tolerated.
 * - Partial state drops after [partialBlobGcMs] of inactivity.
 * - A CRC-verified reassembly emits [BlobArrival] with a locally-computed SHA-256 hash.
 *   `tag` is always `null` here; any application-layer tag is a concern of the proto
 *   layer (Phase 8) or the facade (Phase 10).
 */
internal class BlobReceiver(
    private val partialBlobGcMs: Long,
    private val nowMs: () -> Long = System::currentTimeMillis,
) {
    private data class Partial(
        val total: Int,
        val chunks: Array<ByteArray?>,
        var received: Int,
        val firstSeenMs: Long,
        var lastActivityMs: Long,
    )

    private val mutex = Mutex()
    private val partials = HashMap<Pair<SenderId, BlobId>, Partial>()

    private val _arrivals = MutableSharedFlow<BlobArrival>(
        extraBufferCapacity = 32,
        onBufferOverflow = BufferOverflow.DROP_OLDEST,
    )
    val arrivals: SharedFlow<BlobArrival> = _arrivals.asSharedFlow()

    private val _progress = MutableSharedFlow<BlobReceiveProgress>(
        extraBufferCapacity = 64,
        onBufferOverflow = BufferOverflow.DROP_OLDEST,
    )
    val progress: SharedFlow<BlobReceiveProgress> = _progress.asSharedFlow()

    /**
     * Try to absorb [payload] as a blob chunk from [senderId]. Returns `true` if the
     * payload parsed as a valid envelope (marker + sanity + CRC); `false` otherwise —
     * the caller should treat it as plain text in that case.
     */
    suspend fun accept(senderId: SenderId, payload: ByteArray): Boolean {
        val chunk = BlobEnvelope.tryDecode(payload) ?: return false
        mutex.withLock {
            gcLocked()
            val key = senderId to chunk.blobId
            val now = nowMs()
            val existing = partials[key]
            val p = if (existing == null) {
                val fresh = Partial(
                    total = chunk.total,
                    chunks = arrayOfNulls(chunk.total),
                    received = 0,
                    firstSeenMs = now,
                    lastActivityMs = now,
                )
                partials[key] = fresh
                fresh
            } else {
                if (existing.total != chunk.total) {
                    // Conflicting metadata for the same (sender, blobId). Drop the
                    // chunk; keep the existing partial untouched.
                    return true
                }
                existing
            }
            p.lastActivityMs = now
            if (p.chunks[chunk.idx] == null) {
                p.chunks[chunk.idx] = chunk.bytes
                p.received += 1
            } // else duplicate — ignored.
            _progress.tryEmit(
                BlobReceiveProgress(
                    blobId = chunk.blobId,
                    senderId = senderId,
                    receivedChunks = p.received,
                    totalChunks = p.total,
                    firstSeenAtMs = p.firstSeenMs,
                )
            )
            if (p.received == p.total) {
                partials.remove(key)
                val reassembled = concat(p.chunks)
                val hash = sha256(reassembled)
                _arrivals.tryEmit(
                    BlobArrival(
                        blobId = chunk.blobId,
                        senderId = senderId,
                        bytes = reassembled,
                        hash = hash,
                    )
                )
            }
        }
        return true
    }

    /** Force a GC sweep. Normally called implicitly on every [accept]. */
    suspend fun gc() = mutex.withLock { gcLocked() }

    /** Snapshot of outstanding partials, for diagnostics/tests. */
    suspend fun partialsSnapshot(): List<BlobReceiveProgress> = mutex.withLock {
        partials.map { (key, p) ->
            BlobReceiveProgress(
                blobId = key.second,
                senderId = key.first,
                receivedChunks = p.received,
                totalChunks = p.total,
                firstSeenAtMs = p.firstSeenMs,
            )
        }
    }

    private fun gcLocked() {
        val now = nowMs()
        val it = partials.entries.iterator()
        while (it.hasNext()) {
            val p = it.next().value
            if (now - p.lastActivityMs > partialBlobGcMs) it.remove()
        }
    }

    private fun concat(chunks: Array<ByteArray?>): ByteArray {
        val total = chunks.sumOf { it!!.size }
        val out = ByteArray(total)
        var p = 0
        for (c in chunks) {
            val bytes = c!!
            System.arraycopy(bytes, 0, out, p, bytes.size)
            p += bytes.size
        }
        return out
    }

    private fun sha256(bytes: ByteArray): ByteArray =
        MessageDigest.getInstance("SHA-256").digest(bytes)
}
