package io.tunnelchat.internal.blob

import io.tunnelchat.api.BlobId
import io.tunnelchat.internal.wire.CommandFrame
import io.tunnelchat.internal.wire.readU16Be
import io.tunnelchat.internal.wire.readU32Be
import io.tunnelchat.internal.wire.readU64Be
import io.tunnelchat.internal.wire.writeU16Be
import io.tunnelchat.internal.wire.writeU32Be
import io.tunnelchat.internal.wire.writeU64Be
import java.util.zip.CRC32

/**
 * Blob chunk envelope carried inside a `Text` payload:
 *
 *   [marker:4][blob_id:8][idx:2][total:2][crc32:4][chunk:N]   — 20 B overhead
 *
 * The marker+envelope+CRC gate is a workaround for the absence of a dedicated `Bytes`
 * msg_type in firmware. Marker alone is ~2^-32; combined with the CRC gate, a random
 * payload masquerading as a blob chunk is ~2^-64. See DESIGN §"Wire envelopes".
 *
 * Pure functions; no I/O. All multi-byte fields big-endian (consistent with the rest of
 * the wire package).
 */
internal object BlobEnvelope {

    /** Arbitrary 4-byte magic that does not collide with the proto magic (0xA7). */
    val MARKER: ByteArray = byteArrayOf(0xB1.toByte(), 0x0B, 0xEE.toByte(), 0xF0.toByte())

    const val OVERHEAD: Int = 20
    const val MAX_CHUNK_BYTES: Int = CommandFrame.MAX_TEXT_BYTES - OVERHEAD   // 79
    /** DESIGN sanity gate: reject envelopes claiming more than 255 chunks. */
    const val MAX_TOTAL_CHUNKS: Int = 255

    data class Chunk(
        val blobId: BlobId,
        val idx: Int,
        val total: Int,
        val bytes: ByteArray,
    ) {
        override fun equals(other: Any?): Boolean =
            other is Chunk &&
                blobId == other.blobId &&
                idx == other.idx &&
                total == other.total &&
                bytes.contentEquals(other.bytes)

        override fun hashCode(): Int {
            var r = blobId.hashCode()
            r = 31 * r + idx
            r = 31 * r + total
            r = 31 * r + bytes.contentHashCode()
            return r
        }
    }

    fun encode(blobId: BlobId, idx: Int, total: Int, chunk: ByteArray): ByteArray {
        require(total in 1..MAX_TOTAL_CHUNKS) { "total=$total out of 1..$MAX_TOTAL_CHUNKS" }
        require(idx in 0 until total) { "idx=$idx not in 0..<$total" }
        require(chunk.isNotEmpty() && chunk.size <= MAX_CHUNK_BYTES) {
            "chunk size=${chunk.size} not in 1..$MAX_CHUNK_BYTES"
        }
        val out = ByteArray(OVERHEAD + chunk.size)
        System.arraycopy(MARKER, 0, out, 0, 4)
        writeU64Be(out, 4, blobId.raw)
        writeU16Be(out, 12, idx.toUShort())
        writeU16Be(out, 14, total.toUShort())
        writeU32Be(out, 16, crc32(chunk).toUInt())
        System.arraycopy(chunk, 0, out, OVERHEAD, chunk.size)
        return out
    }

    /**
     * Returns a [Chunk] only if all gates pass: marker match, idx/total sane, non-empty
     * chunk within length cap, and CRC32 matches. Otherwise `null` — caller treats the
     * payload as plain text.
     */
    fun tryDecode(bytes: ByteArray): Chunk? {
        if (bytes.size < OVERHEAD + 1) return null
        if (bytes.size > CommandFrame.MAX_TEXT_BYTES) return null
        if (!markerMatches(bytes)) return null
        val blobId = BlobId(readU64Be(bytes, 4))
        val idx = readU16Be(bytes, 12).toInt()
        val total = readU16Be(bytes, 14).toInt()
        val crc = readU32Be(bytes, 16).toLong() and 0xFFFFFFFFL
        if (total < 1 || total > MAX_TOTAL_CHUNKS) return null
        if (idx < 0 || idx >= total) return null
        val chunk = bytes.copyOfRange(OVERHEAD, bytes.size)
        if (chunk.size > MAX_CHUNK_BYTES) return null
        if (crc32(chunk) != crc) return null
        return Chunk(blobId, idx, total, chunk)
    }

    private fun markerMatches(b: ByteArray): Boolean =
        b[0] == MARKER[0] && b[1] == MARKER[1] && b[2] == MARKER[2] && b[3] == MARKER[3]

    private fun crc32(b: ByteArray): Long {
        val c = CRC32()
        c.update(b)
        return c.value
    }
}
