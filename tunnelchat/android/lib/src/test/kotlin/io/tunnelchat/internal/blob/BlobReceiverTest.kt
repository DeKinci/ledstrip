package io.tunnelchat.internal.blob

import io.tunnelchat.api.BlobArrival
import io.tunnelchat.api.BlobId
import io.tunnelchat.api.BlobReceiveProgress
import io.tunnelchat.api.SenderId
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.security.MessageDigest

@OptIn(ExperimentalCoroutinesApi::class)
class BlobReceiverTest {

    private val sender = SenderId(0x11u)
    private val blobId = BlobId(0x1234_5678_9ABC_DEF0uL)

    private fun sha256(b: ByteArray): ByteArray =
        MessageDigest.getInstance("SHA-256").digest(b)

    @Test
    fun plain_text_not_absorbed_as_blob() = runTest {
        val rx = BlobReceiver(partialBlobGcMs = 60_000)
        val absorbed = rx.accept(sender, "hello".toByteArray())
        assertFalse(absorbed)
        assertTrue(rx.partialsSnapshot().isEmpty())
    }

    @Test
    fun single_chunk_blob_reassembles_immediately() = runTest {
        val rx = BlobReceiver(partialBlobGcMs = 60_000)
        val payload = byteArrayOf(1, 2, 3, 4)
        val frame = BlobEnvelope.encode(blobId, 0, 1, payload)

        val arrivals = mutableListOf<BlobArrival>()
        val job = launch(start = CoroutineStart.UNDISPATCHED) { rx.arrivals.collect { arrivals += it } }

        assertTrue(rx.accept(sender, frame))
        // Give the shared-flow emission time to land.
        kotlinx.coroutines.yield()
        job.cancel()

        assertEquals(1, arrivals.size)
        val a = arrivals[0]
        assertEquals(blobId, a.blobId)
        assertEquals(sender, a.senderId)
        assertArrayEquals(payload, a.bytes)
        assertArrayEquals(sha256(payload), a.hash)
        assertNull(a.tag)
        assertTrue(rx.partialsSnapshot().isEmpty())
    }

    @Test
    fun multi_chunk_reassembly_out_of_order() = runTest {
        val rx = BlobReceiver(partialBlobGcMs = 60_000)
        val full = ByteArray(200) { (it * 7).toByte() }
        val chunks = full.toList().chunked(BlobEnvelope.MAX_CHUNK_BYTES) { it.toByteArray() }
        val total = chunks.size
        assertTrue("needs multiple chunks", total >= 3)

        val arrivals = mutableListOf<BlobArrival>()
        val job = launch(start = CoroutineStart.UNDISPATCHED) { rx.arrivals.collect { arrivals += it } }

        // Deliver out of order: last, middle, ..., first.
        val order = (0 until total).shuffled(java.util.Random(42)).toMutableList()
        for (i in order) {
            val f = BlobEnvelope.encode(blobId, i, total, chunks[i])
            assertTrue(rx.accept(sender, f))
        }
        kotlinx.coroutines.yield()
        job.cancel()

        assertEquals(1, arrivals.size)
        assertArrayEquals(full, arrivals[0].bytes)
        assertTrue(rx.partialsSnapshot().isEmpty())
    }

    @Test
    fun duplicate_chunks_are_idempotent() = runTest {
        val rx = BlobReceiver(partialBlobGcMs = 60_000)
        val c0 = byteArrayOf(1)
        val c1 = byteArrayOf(2)
        val f0 = BlobEnvelope.encode(blobId, 0, 2, c0)
        val f1 = BlobEnvelope.encode(blobId, 1, 2, c1)

        val arrivals = mutableListOf<BlobArrival>()
        val job = launch(start = CoroutineStart.UNDISPATCHED) { rx.arrivals.collect { arrivals += it } }

        rx.accept(sender, f0)
        rx.accept(sender, f0)   // duplicate
        rx.accept(sender, f0)   // duplicate
        // partial, still 1/2.
        assertEquals(1, rx.partialsSnapshot().single().receivedChunks)
        rx.accept(sender, f1)
        kotlinx.coroutines.yield()
        job.cancel()

        assertEquals(1, arrivals.size)
        assertArrayEquals(byteArrayOf(1, 2), arrivals[0].bytes)
    }

    @Test
    fun crc_failure_rejects_payload() = runTest {
        val rx = BlobReceiver(partialBlobGcMs = 60_000)
        val f = BlobEnvelope.encode(blobId, 0, 1, byteArrayOf(9, 9, 9))
        f[BlobEnvelope.OVERHEAD] = 0x00  // corrupt chunk
        assertFalse(rx.accept(sender, f))
        assertTrue(rx.partialsSnapshot().isEmpty())
    }

    @Test
    fun progress_is_emitted_per_chunk() = runTest {
        val rx = BlobReceiver(partialBlobGcMs = 60_000)
        val full = ByteArray(BlobEnvelope.MAX_CHUNK_BYTES * 3) { it.toByte() }
        val chunks = (0 until 3).map {
            full.copyOfRange(it * BlobEnvelope.MAX_CHUNK_BYTES, (it + 1) * BlobEnvelope.MAX_CHUNK_BYTES)
        }

        val progressSeen = mutableListOf<BlobReceiveProgress>()
        val job = launch(start = CoroutineStart.UNDISPATCHED) { rx.progress.collect { progressSeen += it } }

        for (i in 0..2) rx.accept(sender, BlobEnvelope.encode(blobId, i, 3, chunks[i]))
        kotlinx.coroutines.yield()
        job.cancel()

        assertEquals(listOf(1, 2, 3), progressSeen.map { it.receivedChunks })
        assertTrue(progressSeen.all { it.totalChunks == 3 })
    }

    @Test
    fun gc_drops_partials_past_timeout() = runTest {
        var now = 1000L
        val rx = BlobReceiver(partialBlobGcMs = 10_000, nowMs = { now })
        val f = BlobEnvelope.encode(blobId, 0, 2, byteArrayOf(1, 2, 3))
        rx.accept(sender, f)
        assertEquals(1, rx.partialsSnapshot().size)

        now += 5_000   // under timeout
        rx.gc()
        assertEquals(1, rx.partialsSnapshot().size)

        now += 10_001  // over timeout
        rx.gc()
        assertTrue(rx.partialsSnapshot().isEmpty())
    }

    @Test
    fun gc_runs_implicitly_on_accept() = runTest {
        var now = 0L
        val rx = BlobReceiver(partialBlobGcMs = 1_000, nowMs = { now })
        rx.accept(sender, BlobEnvelope.encode(blobId, 0, 2, byteArrayOf(1)))
        assertEquals(1, rx.partialsSnapshot().size)

        now = 2_000
        // A fresh unrelated chunk from a different blob triggers GC of the stale one.
        val other = BlobId(0x42uL)
        rx.accept(sender, BlobEnvelope.encode(other, 0, 2, byteArrayOf(2)))
        val snap = rx.partialsSnapshot()
        assertEquals(1, snap.size)
        assertEquals(other, snap.single().blobId)
    }

    @Test
    fun blobs_from_different_senders_with_same_id_are_independent() = runTest {
        val rx = BlobReceiver(partialBlobGcMs = 60_000)
        val s2 = SenderId(0x22u)
        rx.accept(sender, BlobEnvelope.encode(blobId, 0, 2, byteArrayOf(1)))
        rx.accept(s2,     BlobEnvelope.encode(blobId, 0, 2, byteArrayOf(2)))
        assertEquals(2, rx.partialsSnapshot().size)
    }

    @Test
    fun conflicting_total_for_same_key_drops_new_chunk() = runTest {
        val rx = BlobReceiver(partialBlobGcMs = 60_000)
        rx.accept(sender, BlobEnvelope.encode(blobId, 0, 3, byteArrayOf(1)))
        val snapBefore = rx.partialsSnapshot().single()
        assertEquals(3, snapBefore.totalChunks)
        // Same (sender, blobId) but claims a different total — should be absorbed as a
        // blob but ignored against the existing partial.
        val confused = BlobEnvelope.encode(blobId, 0, 7, byteArrayOf(9))
        assertTrue(rx.accept(sender, confused))
        val snapAfter = rx.partialsSnapshot().single()
        assertEquals(3, snapAfter.totalChunks)  // original partial preserved
        assertEquals(1, snapAfter.receivedChunks)
    }
}
