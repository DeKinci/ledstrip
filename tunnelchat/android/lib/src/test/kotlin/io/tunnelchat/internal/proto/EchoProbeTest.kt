package io.tunnelchat.internal.proto

import io.tunnelchat.api.BlobId
import io.tunnelchat.api.SenderId
import io.tunnelchat.api.TunnelchatError
import io.tunnelchat.builtin.Echo
import io.tunnelchat.builtin.Pong
import io.tunnelchat.internal.blob.BlobEnvelope
import io.tunnelchat.internal.blob.BlobSender
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.async
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class EchoProbeTest {

    private val target = SenderId(9u)

    /** Harness: real BlobSender whose sendChunk captures the single outbound chunk. */
    private class Harness(scope: TestScope) {
        val captured = mutableListOf<ByteArray>()
        val blobSender = BlobSender(
            scope = scope,
            maxBlobBytes = 4096,
            maxInFlightBlobBytes = 4096,
            sendChunk = BlobSender.SendChunk { bytes -> captured += bytes; true },
            newBlobId = { BlobId(0u) },
        )
        val protoSender = ProtoSender(blobSender)
        var now: Long = 1_000L
        val probe = EchoProbe(protoSender, nowMs = { now })
    }

    @Test
    fun probe_resolves_when_pong_from_target_arrives() = runTest {
        val h = Harness(this)
        h.now = 1_000L
        val deferred = async { h.probe.probe(target, timeoutMs = 5_000L) }
        runCurrent()
        // Extract ping_id from the captured outbound bytes.
        assertEquals(1, h.captured.size)
        val chunk = h.captured[0]
        val pingId = extractPingIdFromBlobChunk(chunk)

        h.now = 1_250L
        h.probe.onPong(target, Pong.newBuilder().setPingId(pingId.toInt()).setOriginTs(0).build())

        val result = deferred.await()
        assertTrue(result.isSuccess)
        val echoResult = result.getOrThrow()
        assertEquals(250L, echoResult.rttMs)
        assertEquals(pingId, echoResult.pingId)
    }

    @Test
    fun probe_ignores_pong_from_wrong_target() = runTest {
        val h = Harness(this)
        val deferred = async { h.probe.probe(target, timeoutMs = 500L) }
        runCurrent()
        val pingId = extractPingIdFromBlobChunk(h.captured[0])

        // Wrong sender — should be ignored.
        h.probe.onPong(SenderId(99u), Pong.newBuilder().setPingId(pingId.toInt()).build())
        advanceTimeBy(500L)
        advanceUntilIdle()

        val result = deferred.await()
        assertTrue(result.isFailure)
        assertTrue(result.exceptionOrNull() is TunnelchatError.Timeout)
    }

    @Test
    fun probe_times_out_when_no_pong() = runTest {
        val h = Harness(this)
        val deferred = async { h.probe.probe(target, timeoutMs = 250L) }
        runCurrent()
        advanceTimeBy(250L)
        advanceUntilIdle()

        val result = deferred.await()
        assertTrue(result.isFailure)
        assertTrue(result.exceptionOrNull() is TunnelchatError.Timeout)
    }

    @Test
    fun probe_ignores_pong_with_unknown_pingid() = runTest {
        val h = Harness(this)
        val deferred = async { h.probe.probe(target, timeoutMs = 400L) }
        runCurrent()
        h.probe.onPong(target, Pong.newBuilder().setPingId(424242).build())
        advanceTimeBy(400L)
        advanceUntilIdle()

        val result = deferred.await()
        assertTrue(result.isFailure)
    }

    /** Decodes [blob-envelope][proto-envelope][echo] to extract the ping_id. */
    private fun extractPingIdFromBlobChunk(chunk: ByteArray): UInt {
        val payload = chunk.copyOfRange(BlobEnvelope.OVERHEAD, chunk.size)
        val decoded = ProtoEnvelope.tryDecode(payload)!!
        return Echo.parseFrom(decoded.payload).pingId.toUInt()
    }
}
