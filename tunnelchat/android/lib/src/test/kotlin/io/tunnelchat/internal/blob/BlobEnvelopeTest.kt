package io.tunnelchat.internal.blob

import io.tunnelchat.api.BlobId
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test

class BlobEnvelopeTest {

    private val blobId = BlobId(0xDEADBEEF_CAFEBABEUL)

    @Test
    fun encode_then_decode_round_trips() {
        val payload = ByteArray(40) { it.toByte() }
        val frame = BlobEnvelope.encode(blobId, idx = 3, total = 10, chunk = payload)
        assertEquals(BlobEnvelope.OVERHEAD + payload.size, frame.size)

        val decoded = BlobEnvelope.tryDecode(frame)!!
        assertEquals(blobId, decoded.blobId)
        assertEquals(3, decoded.idx)
        assertEquals(10, decoded.total)
        assertArrayEquals(payload, decoded.bytes)
    }

    @Test
    fun decode_rejects_non_marker_bytes() {
        val plain = "hello world".toByteArray()
        assertNull(BlobEnvelope.tryDecode(plain))
    }

    @Test
    fun decode_rejects_too_short_frame() {
        assertNull(BlobEnvelope.tryDecode(ByteArray(BlobEnvelope.OVERHEAD)))
        assertNull(BlobEnvelope.tryDecode(ByteArray(5)))
    }

    @Test
    fun decode_rejects_marker_only_with_crc_mismatch() {
        val payload = byteArrayOf(1, 2, 3, 4)
        val frame = BlobEnvelope.encode(blobId, 0, 1, payload)
        // Flip one byte of the chunk — CRC no longer matches.
        frame[BlobEnvelope.OVERHEAD] = (frame[BlobEnvelope.OVERHEAD].toInt() xor 0x01).toByte()
        assertNull(BlobEnvelope.tryDecode(frame))
    }

    @Test
    fun decode_rejects_marker_collision_with_bad_header() {
        // Marker matches, but total=0 → sanity gate trips.
        val fake = ByteArray(BlobEnvelope.OVERHEAD + 4)
        System.arraycopy(BlobEnvelope.MARKER, 0, fake, 0, 4)
        // total bytes (offset 14..15) remain 0 — rejected.
        // Fill something that wouldn't produce a valid CRC anyway.
        fake[BlobEnvelope.OVERHEAD] = 0xAA.toByte()
        assertNull(BlobEnvelope.tryDecode(fake))
    }

    @Test
    fun decode_rejects_idx_at_or_past_total() {
        val payload = byteArrayOf(9, 9, 9)
        // Manually craft: idx=5, total=5 (idx must be < total).
        val fake = BlobEnvelope.encode(blobId, 0, 5, payload)
        // Overwrite idx field (bytes 12..13) to 5.
        fake[12] = 0; fake[13] = 5
        // CRC still covers chunk; but idx==total must fail sanity.
        assertNull(BlobEnvelope.tryDecode(fake))
    }

    @Test
    fun decode_rejects_total_above_sanity_cap() {
        val payload = byteArrayOf(1)
        val fake = BlobEnvelope.encode(blobId, 0, 10, payload)
        // Overwrite total to 256 (exceeds MAX_TOTAL_CHUNKS = 255).
        fake[14] = 0x01; fake[15] = 0x00
        assertNull(BlobEnvelope.tryDecode(fake))
    }

    @Test(expected = IllegalArgumentException::class)
    fun encode_rejects_oversize_chunk() {
        BlobEnvelope.encode(blobId, 0, 1, ByteArray(BlobEnvelope.MAX_CHUNK_BYTES + 1) { 0 })
    }

    @Test(expected = IllegalArgumentException::class)
    fun encode_rejects_empty_chunk() {
        BlobEnvelope.encode(blobId, 0, 1, ByteArray(0))
    }

    @Test(expected = IllegalArgumentException::class)
    fun encode_rejects_idx_out_of_range() {
        BlobEnvelope.encode(blobId, 3, 3, byteArrayOf(1))
    }

    @Test
    fun max_chunk_boundary() {
        val payload = ByteArray(BlobEnvelope.MAX_CHUNK_BYTES) { (it and 0xFF).toByte() }
        val frame = BlobEnvelope.encode(blobId, 0, 1, payload)
        assertEquals(99, frame.size)  // MAX_TEXT_BYTES
        val decoded = BlobEnvelope.tryDecode(frame)!!
        assertArrayEquals(payload, decoded.bytes)
    }

    @Test
    fun different_blob_ids_round_trip_distinctly() {
        val a = BlobEnvelope.encode(BlobId(0u), 0, 1, byteArrayOf(7))
        val b = BlobEnvelope.encode(BlobId(ULong.MAX_VALUE), 0, 1, byteArrayOf(7))
        assertEquals(BlobId(0u), BlobEnvelope.tryDecode(a)!!.blobId)
        assertEquals(BlobId(ULong.MAX_VALUE), BlobEnvelope.tryDecode(b)!!.blobId)
    }
}
