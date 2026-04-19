package io.tunnelchat.api

import kotlinx.coroutines.flow.StateFlow

/**
 * Handle to an outbound blob.
 *
 * The mesh is broadcast + digest-sync pull; the sender cannot observe which peers
 * reassembled the blob. Completion semantics are "all chunks have been handed to
 * the BLE transport," nothing stronger.
 */
abstract class BlobHandle internal constructor() {
    abstract val blobId: BlobId
    abstract val progress: StateFlow<BlobSendProgress>

    /**
     * Suspends until every chunk has been written to the BLE transport, or the send
     * fails. Does NOT imply any peer reassembled the blob — the mesh provides no such
     * signal. Use for UI "sending…" spinners only.
     */
    abstract suspend fun awaitTransmitted(): Result<Unit>

    abstract fun cancel()
}

sealed class BlobSendProgress {
    /** Byte-budget cap (`TunnelchatConfig.maxInFlightBlobBytes`) is full. Waiting. */
    data class Queued(val totalChunks: Int) : BlobSendProgress()
    data class InFlight(val totalChunks: Int, val sentChunks: Int) : BlobSendProgress()
    /** All chunks handed to the BLE transport. */
    data class Transmitted(val totalChunks: Int) : BlobSendProgress()
    data class Failed(val error: TunnelchatError) : BlobSendProgress()
}

data class BlobReceiveProgress(
    val blobId: BlobId,
    val senderId: SenderId,
    val receivedChunks: Int,
    val totalChunks: Int,
    val firstSeenAtMs: Long,
)

/** Completed, CRC-verified reassembly. [hash] is receiver-local truth (SHA-256). */
data class BlobArrival(
    val blobId: BlobId,
    val senderId: SenderId,
    val bytes: ByteArray,
    val hash: ByteArray,
    val tag: String?,
) {
    override fun equals(other: Any?): Boolean =
        other is BlobArrival &&
            blobId == other.blobId &&
            senderId == other.senderId &&
            bytes.contentEquals(other.bytes) &&
            hash.contentEquals(other.hash) &&
            tag == other.tag

    override fun hashCode(): Int {
        var r = blobId.hashCode()
        r = 31 * r + senderId.hashCode()
        r = 31 * r + bytes.contentHashCode()
        r = 31 * r + hash.contentHashCode()
        r = 31 * r + (tag?.hashCode() ?: 0)
        return r
    }
}
