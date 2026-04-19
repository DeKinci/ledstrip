package io.tunnelchat.internal.blob

import io.tunnelchat.api.BlobId
import io.tunnelchat.api.BlobSendProgress
import io.tunnelchat.api.SenderId
import io.tunnelchat.api.TunnelchatError
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.async
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class BlobSenderTest {

    /** Records every chunk handed to the BLE layer. Can be paused / made to fail. */
    private class CapturingTransport : BlobSender.SendChunk {
        val sent = mutableListOf<ByteArray>()
        var acceptSends = true
        var gate: CompletableDeferred<Unit>? = null
        override suspend fun send(payload: ByteArray): Boolean {
            gate?.await()
            if (!acceptSends) return false
            sent += payload
            return true
        }
    }

    private fun fixedIdSeq(): () -> BlobId {
        var n = 0uL
        return { BlobId(n++) }
    }

    @Test
    fun small_blob_single_chunk_transmitted() = runTest {
        val tx = CapturingTransport()
        val sender = BlobSender(
            scope = this,
            maxBlobBytes = 2048,
            maxInFlightBlobBytes = 8 * 1024,
            sendChunk = tx,
            newBlobId = fixedIdSeq(),
        )
        val handle = sender.enqueue(byteArrayOf(1, 2, 3))
        val result = handle.awaitTransmitted()
        assertTrue(result.isSuccess)
        assertEquals(1, tx.sent.size)
        val decoded = BlobEnvelope.tryDecode(tx.sent[0])!!
        assertEquals(0, decoded.idx)
        assertEquals(1, decoded.total)
        assertArrayEquals(byteArrayOf(1, 2, 3), decoded.bytes)
        assertEquals(BlobSendProgress.Transmitted(1), handle.progress.value)
    }

    @Test
    fun multi_chunk_blob_emits_all_chunks_in_order() = runTest {
        val tx = CapturingTransport()
        val sender = BlobSender(
            scope = this,
            maxBlobBytes = 2048,
            maxInFlightBlobBytes = 8 * 1024,
            sendChunk = tx,
            newBlobId = fixedIdSeq(),
        )
        val payload = ByteArray(200) { it.toByte() }
        val handle = sender.enqueue(payload)
        handle.awaitTransmitted().getOrThrow()

        val total = (payload.size + BlobEnvelope.MAX_CHUNK_BYTES - 1) / BlobEnvelope.MAX_CHUNK_BYTES
        assertEquals(total, tx.sent.size)
        val reassembled = ByteArray(payload.size)
        var off = 0
        tx.sent.forEachIndexed { i, f ->
            val d = BlobEnvelope.tryDecode(f)!!
            assertEquals(i, d.idx)
            assertEquals(total, d.total)
            System.arraycopy(d.bytes, 0, reassembled, off, d.bytes.size)
            off += d.bytes.size
        }
        assertArrayEquals(payload, reassembled)
    }

    @Test
    fun oversize_blob_fails_with_payload_too_large() = runTest {
        val tx = CapturingTransport()
        val sender = BlobSender(
            scope = this,
            maxBlobBytes = 100,
            maxInFlightBlobBytes = 8 * 1024,
            sendChunk = tx,
            newBlobId = fixedIdSeq(),
        )
        val handle = sender.enqueue(ByteArray(101) { 0 })
        val r = handle.awaitTransmitted()
        assertTrue(r.isFailure)
        assertTrue(r.exceptionOrNull() is TunnelchatError.PayloadTooLarge)
        assertEquals(0, tx.sent.size)
    }

    @Test
    fun disconnected_transport_fails_with_ble_disconnected() = runTest {
        val tx = CapturingTransport().apply { acceptSends = false }
        val sender = BlobSender(
            scope = this,
            maxBlobBytes = 2048,
            maxInFlightBlobBytes = 8 * 1024,
            sendChunk = tx,
            newBlobId = fixedIdSeq(),
        )
        val handle = sender.enqueue(byteArrayOf(1, 2, 3))
        val r = handle.awaitTransmitted()
        assertTrue(r.isFailure)
        assertEquals(TunnelchatError.BleDisconnected, r.exceptionOrNull())
    }

    @Test
    fun budget_queues_second_blob_until_first_drains() = runTest {
        val tx = CapturingTransport()
        tx.gate = CompletableDeferred()  // block sends until released
        val sender = BlobSender(
            scope = this,
            maxBlobBytes = 2048,
            maxInFlightBlobBytes = 100,    // tight cap
            sendChunk = tx,
            newBlobId = fixedIdSeq(),
        )
        val h1 = sender.enqueue(ByteArray(80) { 1 })   // reserves 80 of 100
        val h2 = sender.enqueue(ByteArray(80) { 2 })   // would push to 160 → queued
        runCurrent()

        // h2 must still be Queued while h1 is mid-flight.
        assertTrue(h2.progress.value is BlobSendProgress.Queued)

        // Let h1's chunks through.
        tx.gate!!.complete(Unit)
        advanceUntilIdle()

        assertEquals(BlobSendProgress.Transmitted((80 + BlobEnvelope.MAX_CHUNK_BYTES - 1) / BlobEnvelope.MAX_CHUNK_BYTES), h1.progress.value)
        assertTrue(h2.progress.value is BlobSendProgress.Transmitted)
    }

    @Test
    fun cancel_during_queue_releases_slot_for_next() = runTest {
        val tx = CapturingTransport()
        tx.gate = CompletableDeferred()
        val sender = BlobSender(
            scope = this,
            maxBlobBytes = 2048,
            maxInFlightBlobBytes = 100,
            sendChunk = tx,
            newBlobId = fixedIdSeq(),
        )
        val h1 = sender.enqueue(ByteArray(80) { 1 })
        val h2 = sender.enqueue(ByteArray(80) { 2 })
        val h3 = sender.enqueue(ByteArray(80) { 3 })
        runCurrent()

        // h2, h3 both queued.
        assertTrue(h2.progress.value is BlobSendProgress.Queued)
        assertTrue(h3.progress.value is BlobSendProgress.Queued)

        // Cancel h2; it should fail and not unblock h3 (still waiting on h1).
        h2.cancel()
        runCurrent()
        assertTrue(h2.progress.value is BlobSendProgress.Failed)
        assertTrue(h3.progress.value is BlobSendProgress.Queued)

        // Finish h1 — budget drops to 0; h3 admitted.
        tx.gate!!.complete(Unit)
        advanceUntilIdle()
        assertTrue(h3.progress.value is BlobSendProgress.Transmitted)
    }

    @Test
    fun send_failure_mid_blob_fails_handle_and_releases_budget() = runTest {
        // Transport succeeds for the first N sends, then fails every subsequent send.
        class FlakyTransport(val okUntil: Int) : BlobSender.SendChunk {
            val sent = mutableListOf<ByteArray>()
            override suspend fun send(payload: ByteArray): Boolean {
                if (sent.size >= okUntil) return false
                sent += payload
                return true
            }
        }
        val tx = FlakyTransport(okUntil = 1)
        val sender = BlobSender(
            scope = this,
            maxBlobBytes = 500,
            maxInFlightBlobBytes = 500,
            sendChunk = tx,
            newBlobId = fixedIdSeq(),
        )
        val blobBytes = BlobEnvelope.MAX_CHUNK_BYTES * 2   // 158 bytes, 2 chunks
        val h1 = sender.enqueue(ByteArray(blobBytes) { 1 })
        val r = h1.awaitTransmitted()
        assertTrue(r.isFailure)
        assertEquals(TunnelchatError.BleDisconnected, r.exceptionOrNull())
        assertEquals(1, tx.sent.size)    // only the first chunk made it out

        // Budget fully released → a fresh blob at the same budget size succeeds.
        val tx2 = FlakyTransport(okUntil = Int.MAX_VALUE)
        val sender2 = BlobSender(
            scope = this,
            maxBlobBytes = 500,
            maxInFlightBlobBytes = 500,
            sendChunk = tx2,
            newBlobId = fixedIdSeq(),
        )
        val h2 = sender2.enqueue(byteArrayOf(9))
        h2.awaitTransmitted().getOrThrow()
    }

    @Test
    fun loopback_round_trip_with_receiver() = runTest {
        val rx = BlobReceiver(partialBlobGcMs = 60_000)
        val senderId = SenderId(0x11u)
        val loopback = BlobSender.SendChunk { payload ->
            rx.accept(senderId, payload)
        }
        val sender = BlobSender(
            scope = this,
            maxBlobBytes = 2048,
            maxInFlightBlobBytes = 8 * 1024,
            sendChunk = loopback,
            newBlobId = fixedIdSeq(),
        )
        val payload = ByteArray(400) { (it * 3).toByte() }

        val arrivals = mutableListOf<io.tunnelchat.api.BlobArrival>()
        val col = launch(start = CoroutineStart.UNDISPATCHED) { rx.arrivals.collect { arrivals += it } }

        val handle = sender.enqueue(payload)
        handle.awaitTransmitted().getOrThrow()
        runCurrent()
        col.cancel()

        assertEquals(1, arrivals.size)
        assertArrayEquals(payload, arrivals[0].bytes)
        assertEquals(senderId, arrivals[0].senderId)
        assertEquals(handle.blobId, arrivals[0].blobId)
    }

    @Test(expected = IllegalArgumentException::class)
    fun empty_blob_is_programmer_error() = runTest {
        val sender = BlobSender(
            scope = this,
            maxBlobBytes = 2048,
            maxInFlightBlobBytes = 8 * 1024,
            sendChunk = CapturingTransport(),
            newBlobId = fixedIdSeq(),
        )
        sender.enqueue(ByteArray(0))
    }
}
