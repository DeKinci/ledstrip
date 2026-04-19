package io.tunnelchat.internal.protocol

import io.tunnelchat.api.Presence
import io.tunnelchat.api.SenderId
import io.tunnelchat.api.Seq
import io.tunnelchat.api.TunnelchatError
import io.tunnelchat.internal.wire.CommandFrame
import io.tunnelchat.internal.wire.Notification
import io.tunnelchat.internal.wire.NotificationFrame
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.async
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class CommandDispatcherTest {

    // ── Response-frame builders ──────────────────────────────────────────────

    private fun stateRespFrame(vararg entries: IntArray): ByteArray {
        // Each entry is an 8-byte IntArray: [senderId, highHi, highLo, locHi, locLo, nodeA, nodeB, presence]
        val out = ByteArray(2 + entries.size * 8)
        out[0] = NotificationFrame.RESP_STATE
        out[1] = entries.size.toByte()
        var p = 2
        for (e in entries) {
            for (i in 0 until 8) out[p + i] = e[i].toByte()
            p += 8
        }
        return out
    }

    private fun selfInfoFrame(deviceId: Int, clock: UInt, active: Int, boot: UInt): ByteArray {
        val out = ByteArray(11)
        out[0] = NotificationFrame.RESP_SELF_INFO
        out[1] = deviceId.toByte()
        out[2] = ((clock.toLong() ushr 24) and 0xFF).toByte()
        out[3] = ((clock.toLong() ushr 16) and 0xFF).toByte()
        out[4] = ((clock.toLong() ushr 8) and 0xFF).toByte()
        out[5] = (clock.toLong() and 0xFF).toByte()
        out[6] = active.toByte()
        out[7] = ((boot.toLong() ushr 24) and 0xFF).toByte()
        out[8] = ((boot.toLong() ushr 16) and 0xFF).toByte()
        out[9] = ((boot.toLong() ushr 8) and 0xFF).toByte()
        out[10] = (boot.toLong() and 0xFF).toByte()
        return out
    }

    private fun incomingTextFrame(senderId: Int, seq: Int, ts: UInt, text: String, push: Boolean): ByteArray {
        val bytes = text.toByteArray()
        val out = ByteArray(9 + 1 + bytes.size)
        out[0] = if (push) NotificationFrame.NOTIFY_INCOMING else NotificationFrame.RESP_MESSAGE
        out[1] = senderId.toByte()
        out[2] = ((seq ushr 8) and 0xFF).toByte()
        out[3] = (seq and 0xFF).toByte()
        out[4] = ((ts.toLong() ushr 24) and 0xFF).toByte()
        out[5] = ((ts.toLong() ushr 16) and 0xFF).toByte()
        out[6] = ((ts.toLong() ushr 8) and 0xFF).toByte()
        out[7] = (ts.toLong() and 0xFF).toByte()
        out[8] = 0x02  // TEXT
        out[9] = bytes.size.toByte()
        System.arraycopy(bytes, 0, out, 10, bytes.size)
        return out
    }

    private fun presenceFrame(senderId: Int, presenceByte: Int): ByteArray =
        byteArrayOf(NotificationFrame.NOTIFY_PRESENCE, senderId.toByte(), presenceByte.toByte())

    // ── Fire-and-forget command tests ────────────────────────────────────────

    @Test
    fun setClock_sendsCmd01WithBigEndianTimestamp() = runTest {
        val t = FakeTransport()
        val d = CommandDispatcher(t)
        d.start(backgroundScope)
        val r = d.setClock(0x01020304U)
        assertTrue(r.isSuccess)
        assertEquals(1, t.sent.size)
        assertArrayEquals(byteArrayOf(0x01, 0x01, 0x02, 0x03, 0x04), t.sent[0])
    }

    @Test
    fun setLocation_sendsCmd02WithTwoBytes() = runTest {
        val t = FakeTransport()
        val d = CommandDispatcher(t)
        d.start(backgroundScope)
        val r = d.setLocation(0x11u, 0x22u)
        assertTrue(r.isSuccess)
        assertArrayEquals(byteArrayOf(0x02, 0x11, 0x22), t.sent[0])
    }

    @Test
    fun sendText_tooLarge_returnsPayloadTooLargeWithoutSending() = runTest {
        val t = FakeTransport()
        val d = CommandDispatcher(t)
        d.start(backgroundScope)
        val r = d.sendText(ByteArray(100))
        assertFalse(r.isSuccess)
        val err = r.exceptionOrNull()
        assertTrue(err is TunnelchatError.PayloadTooLarge)
        assertEquals(CommandFrame.MAX_TEXT_BYTES, (err as TunnelchatError.PayloadTooLarge).limitBytes)
        assertTrue("transport must not be written", t.sent.isEmpty())
    }

    @Test
    fun sendText_atMaxSize_framedAs03LenBytes() = runTest {
        val t = FakeTransport()
        val d = CommandDispatcher(t)
        d.start(backgroundScope)
        val payload = ByteArray(CommandFrame.MAX_TEXT_BYTES) { (it and 0x7F).toByte() }
        val r = d.sendText(payload)
        assertTrue(r.isSuccess)
        val f = t.sent[0]
        assertEquals(2 + payload.size, f.size)
        assertEquals(0x03.toByte(), f[0])
        assertEquals(payload.size.toByte(), f[1])
    }

    @Test
    fun fireAndForget_returnsBleDisconnectedWhenTransportRejects() = runTest {
        val t = FakeTransport(acceptSends = false)
        val d = CommandDispatcher(t)
        d.start(backgroundScope)
        val r = d.setClock(0u)
        assertTrue(r.isFailure)
        assertTrue(r.exceptionOrNull() is TunnelchatError.BleDisconnected)
    }

    @Test
    fun getMessages_sendsCmd05WithSenderAndSeq() = runTest {
        val t = FakeTransport()
        val d = CommandDispatcher(t)
        d.start(backgroundScope)
        val r = d.getMessages(SenderId(0x2Au), Seq(0x1234u))
        assertTrue(r.isSuccess)
        assertArrayEquals(byteArrayOf(0x05, 0x2A, 0x12, 0x34), t.sent[0])
    }

    // ── Correlated command tests ─────────────────────────────────────────────

    @Test
    fun getState_roundTripsToStateResp() = runTest {
        val t = FakeTransport()
        val d = CommandDispatcher(t)
        d.start(backgroundScope)

        val deferred = async { d.getState(timeoutMs = 2_000L) }
        runCurrent()
        // Command was dispatched before we delivered the response.
        assertEquals(1, t.sent.size)
        assertEquals(0x04.toByte(), t.sent[0][0])

        t.deliver(stateRespFrame(intArrayOf(3, 0, 7, 0, 5, 0xAA, 0xBB, 0)))
        val result = deferred.await()
        assertTrue(result.isSuccess)
        val resp = result.getOrThrow()
        assertEquals(1, resp.entries.size)
        val e = resp.entries[0]
        assertEquals(SenderId(3u), e.senderId)
        assertEquals(Seq(7u), e.highSeq)
        assertEquals(Seq(5u), e.locSeq)
        assertEquals(0xAA.toUByte(), e.nodeA)
        assertEquals(0xBB.toUByte(), e.nodeB)
        assertEquals(Presence.Online, e.presence)
    }

    @Test
    fun getSelfInfo_roundTripsToSelfInfoResp() = runTest {
        val t = FakeTransport()
        val d = CommandDispatcher(t)
        d.start(backgroundScope)

        val deferred = async { d.getSelfInfo(timeoutMs = 2_000L) }
        runCurrent()
        assertEquals(0x06.toByte(), t.sent[0][0])

        t.deliver(selfInfoFrame(deviceId = 7, clock = 0x11223344U, active = 2, boot = 0xDEADBEEFU))
        val resp = deferred.await().getOrThrow()
        assertEquals(7.toUByte(), resp.deviceId)
        assertEquals(0x11223344U, resp.clockUnix)
        assertEquals(2.toUByte(), resp.activeSenders)
        assertEquals(0xDEADBEEFU, resp.bootCount)
    }

    @Test
    fun getState_timesOutWhenNoResponse() = runTest {
        val t = FakeTransport()
        val d = CommandDispatcher(t)
        d.start(backgroundScope)

        val deferred = async { d.getState(timeoutMs = 100L) }
        runCurrent()
        advanceTimeBy(150L)
        val result = deferred.await()
        assertTrue(result.isFailure)
        assertTrue(result.exceptionOrNull() is TunnelchatError.Timeout)
    }

    @Test
    fun getState_returnsBleDisconnectedWhenSendRejects() = runTest {
        val t = FakeTransport(acceptSends = false)
        val d = CommandDispatcher(t)
        d.start(backgroundScope)
        val r = d.getState(timeoutMs = 500L)
        assertTrue(r.isFailure)
        assertTrue(r.exceptionOrNull() is TunnelchatError.BleDisconnected)
    }

    @Test
    fun commands_serialiseThroughMutex() = runTest {
        val t = FakeTransport()
        val d = CommandDispatcher(t)
        d.start(backgroundScope)

        val first = async { d.getState(timeoutMs = 5_000L) }
        runCurrent()
        // Second getState must not have written its frame yet — first holds the mutex.
        val second = async { d.getState(timeoutMs = 5_000L) }
        runCurrent()
        assertEquals("only first getState should have been sent", 1, t.sent.size)

        t.deliver(stateRespFrame())  // empty state, completes first
        runCurrent()  // do NOT advanceUntilIdle here: that would fire second's 5s timeout
        assertEquals("second getState frame sent after first completes", 2, t.sent.size)
        assertTrue(first.await().isSuccess)

        t.deliver(stateRespFrame())
        runCurrent()
        val secondRes = second.await()
        assertTrue("second getState should succeed, got ${secondRes.exceptionOrNull()}", secondRes.isSuccess)
    }

    // ── Fan-out tests ────────────────────────────────────────────────────────

    @Test
    fun incomingMessages_receivesPushFrames() = runTest {
        val t = FakeTransport()
        val d = CommandDispatcher(t)
        d.start(backgroundScope)

        val first = async { d.incomingMessages.first() }
        runCurrent()
        t.deliver(incomingTextFrame(senderId = 3, seq = 1, ts = 1000U, text = "hi", push = true))
        val got = first.await()
        assertTrue(got.push)
        assertEquals(SenderId(3u), got.senderId)
        assertEquals(Seq(1u), got.seq)
    }

    @Test
    fun messageReplies_receivesRespFrames_notIncoming() = runTest {
        val t = FakeTransport()
        val d = CommandDispatcher(t)
        d.start(backgroundScope)

        val reply = async { d.messageReplies.first() }
        runCurrent()
        t.deliver(incomingTextFrame(senderId = 4, seq = 2, ts = 2000U, text = "reply", push = false))
        val got = reply.await()
        assertFalse(got.push)
        assertEquals(SenderId(4u), got.senderId)
    }

    @Test
    fun presenceEvents_receivesPresenceFrames() = runTest {
        val t = FakeTransport()
        val d = CommandDispatcher(t)
        d.start(backgroundScope)

        val ev = async { d.presenceEvents.first() }
        runCurrent()
        t.deliver(presenceFrame(senderId = 5, presenceByte = 1))
        val got = ev.await()
        assertEquals(SenderId(5u), got.senderId)
        assertEquals(Presence.Stale, got.presence)
    }

    @Test
    fun malformed_surfaceOnMalformedFlow() = runTest {
        val t = FakeTransport()
        val d = CommandDispatcher(t)
        d.start(backgroundScope)

        val m = async { d.malformed.first() }
        runCurrent()
        t.deliver(byteArrayOf(0xFF.toByte()))  // unknown opcode
        val got = m.await()
        assertTrue(got.reason.contains("unknown"))
    }

    @Test
    fun pushesInterleaveWithCorrelatedAwait() = runTest {
        val t = FakeTransport()
        val d = CommandDispatcher(t)
        d.start(backgroundScope)

        val stateDeferred = async { d.getState(timeoutMs = 2_000L) }
        runCurrent()

        // A push notification arrives while getState is outstanding.
        val incoming = async { d.incomingMessages.first() }
        runCurrent()
        t.deliver(incomingTextFrame(senderId = 9, seq = 1, ts = 0U, text = "x", push = true))
        assertEquals(SenderId(9u), incoming.await().senderId)

        // The StateResp still completes the getState await.
        t.deliver(stateRespFrame())
        val state = stateDeferred.await().getOrThrow()
        assertTrue(state.entries.isEmpty())
    }

    // ── Lifecycle ────────────────────────────────────────────────────────────

    @Test
    fun start_isIdempotent() = runTest {
        val t = FakeTransport()
        val d = CommandDispatcher(t)
        d.start(backgroundScope)
        d.start(backgroundScope)
        // No exception, no duplicate collection: a single StateResp resolves a single getState.
        val r = async { d.getState(timeoutMs = 2_000L) }
        runCurrent()
        t.deliver(stateRespFrame())
        assertTrue(r.await().isSuccess)
    }

    @Test
    fun close_failsInFlightAwaiter() = runTest {
        val t = FakeTransport()
        val d = CommandDispatcher(t)
        d.start(backgroundScope)

        val deferred = async { runCatching { d.getState(timeoutMs = 5_000L) } }
        runCurrent()
        d.close()
        runCurrent()
        advanceUntilIdle()
        // The awaiter is cancelled; the enclosing runCatching captures a CancellationException.
        val outer = deferred.await()
        // Either the result is a Kotlin Result.failure (cancellation) or isCancelled.
        assertTrue(outer.isFailure)
    }

    @Test
    fun malformedFrames_doNotCompletePendingRequests() = runTest {
        val t = FakeTransport()
        val d = CommandDispatcher(t)
        d.start(backgroundScope)

        val deferred = async { d.getState(timeoutMs = 200L) }
        runCurrent()
        t.deliver(byteArrayOf(0xFF.toByte()))  // garbage; must not resolve
        runCurrent()
        advanceTimeBy(250L)
        val r = deferred.await()
        assertTrue(r.isFailure)
        assertTrue(r.exceptionOrNull() is TunnelchatError.Timeout)
    }

    @Test
    fun stateResp_withoutOutstandingRequest_isDroppedSilently() = runTest {
        val t = FakeTransport()
        val d = CommandDispatcher(t)
        d.start(backgroundScope)
        // No getState pending. Stale StateResp must not crash or stick.
        t.deliver(stateRespFrame())
        runCurrent()
        advanceUntilIdle()
        // Subsequent getState still works end-to-end.
        val r = async { d.getState(timeoutMs = 500L) }
        runCurrent()
        t.deliver(stateRespFrame())
        assertTrue(r.await().isSuccess)
    }

}
