package io.tunnelchat.internal.protocol

import io.tunnelchat.api.BlobId
import io.tunnelchat.api.Message
import io.tunnelchat.api.Presence
import io.tunnelchat.api.SenderId
import io.tunnelchat.api.Seq
import io.tunnelchat.builtin.Echo
import io.tunnelchat.internal.archive.MessageStore
import io.tunnelchat.internal.archive.PresenceStore
import io.tunnelchat.internal.blob.BlobEnvelope
import io.tunnelchat.internal.blob.BlobReceiver
import io.tunnelchat.internal.proto.BuiltinSchemas
import io.tunnelchat.internal.proto.ProtoEnvelope
import io.tunnelchat.internal.proto.ProtoReceiver
import io.tunnelchat.internal.proto.ProtoRegistryImpl
import io.tunnelchat.internal.wire.NotificationFrame
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.async
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.take
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class SessionCoordinatorTest {

    private class Harness(
        scope: CoroutineScope,
        val onEchoCb: (SenderId, Echo) -> Unit = { _, _ -> },
    ) {
        val transport = FakeTransport()
        val dispatcher = CommandDispatcher(transport).also { it.start(scope) }
        val msgDao = FakeMessageDao()
        val msgStore = MessageStore(msgDao)
        val presenceDao = FakePresenceDao()
        val presenceStore = PresenceStore(presenceDao)
        val blobReceiver = BlobReceiver(partialBlobGcMs = 60_000L, nowMs = { 0L })
        val registry = ProtoRegistryImpl()
        val protoReceiver = ProtoReceiver(
            registry = registry,
            onEcho = { sid, e -> onEchoCb(sid, e) },
            onPong = { _, _ -> },
        )
        val gapFiller = GapFiller(dispatcher, msgStore)
        val coordinator = SessionCoordinator(
            dispatcher = dispatcher,
            blobReceiver = blobReceiver,
            protoReceiver = protoReceiver,
            messageStore = msgStore,
            presenceStore = presenceStore,
            gapFiller = gapFiller,
            nowMs = { 100L },
        )
    }

    private fun messageFrame(push: Boolean, senderId: Int, seq: Int, ts: UInt, text: ByteArray): ByteArray {
        val out = ByteArray(9 + 1 + text.size)
        out[0] = if (push) NotificationFrame.NOTIFY_INCOMING else NotificationFrame.RESP_MESSAGE
        out[1] = senderId.toByte()
        out[2] = ((seq ushr 8) and 0xFF).toByte()
        out[3] = (seq and 0xFF).toByte()
        out[4] = ((ts.toLong() ushr 24) and 0xFF).toByte()
        out[5] = ((ts.toLong() ushr 16) and 0xFF).toByte()
        out[6] = ((ts.toLong() ushr 8) and 0xFF).toByte()
        out[7] = (ts.toLong() and 0xFF).toByte()
        out[8] = 0x02
        out[9] = text.size.toByte()
        System.arraycopy(text, 0, out, 10, text.size)
        return out
    }

    private fun presenceFrame(senderId: Int, presence: Int): ByteArray =
        byteArrayOf(NotificationFrame.NOTIFY_PRESENCE, senderId.toByte(), presence.toByte())

    @Test
    fun incoming_text_stored_and_emitted() = runTest {
        val h = Harness(backgroundScope)
        h.coordinator.start(backgroundScope)
        val awaited = async { h.coordinator.incomingMessages.first() }
        runCurrent()

        h.transport.deliver(messageFrame(push = true, senderId = 1, seq = 5, ts = 100u, text = "hi".toByteArray()))
        val env = awaited.await()

        assertEquals(SenderId(1u), env.senderId)
        assertEquals(Seq(5u), env.seq)
        assertEquals(1, h.msgDao.count())
        assertEquals(Seq(5u), h.coordinator.peers.value[SenderId(1u)]!!.highSeq)
    }

    @Test
    fun duplicate_message_not_re_emitted() = runTest {
        val h = Harness(backgroundScope)
        h.coordinator.start(backgroundScope)
        val awaited = async { h.coordinator.incomingMessages.first() }
        runCurrent()

        val frame = messageFrame(true, 1, 5, 100u, "hi".toByteArray())
        h.transport.deliver(frame)
        h.transport.deliver(frame)
        awaited.await()
        advanceUntilIdle()

        assertEquals(1, h.msgDao.count())
    }

    @Test
    fun text_that_is_blob_chunk_absorbed_not_stored() = runTest {
        val h = Harness(backgroundScope)
        h.coordinator.start(backgroundScope)
        runCurrent()

        val chunk = BlobEnvelope.encode(BlobId(42u), 0, 1, "hello".toByteArray())
        h.transport.deliver(messageFrame(true, senderId = 3, seq = 1, ts = 0u, text = chunk))
        advanceUntilIdle()

        assertEquals(0, h.msgDao.count())
    }

    @Test
    fun presence_event_updates_peers_and_store() = runTest {
        val h = Harness(backgroundScope)
        h.coordinator.start(backgroundScope)
        val awaited = async { h.coordinator.peers.first { it.isNotEmpty() } }
        runCurrent()

        h.transport.deliver(presenceFrame(senderId = 1, presence = Presence.Offline.ordinal))
        val peers = awaited.await()

        val peer = peers[SenderId(1u)]
        assertEquals(Presence.Offline, peer!!.presence)
        assertEquals(Presence.Offline, h.presenceStore.get(SenderId(1u))!!.presence)
    }

    @Test
    fun raw_blob_arrival_emitted_on_incoming_blobs() = runTest {
        val h = Harness(backgroundScope)
        h.coordinator.start(backgroundScope)
        val awaited = async { h.coordinator.incomingBlobs.first() }
        runCurrent()

        val raw = "raw payload".toByteArray()
        val chunk = BlobEnvelope.encode(BlobId(7u), 0, 1, raw)
        h.transport.deliver(messageFrame(true, senderId = 5, seq = 1, ts = 0u, text = chunk))
        val arrival = awaited.await()

        assertArrayEquals(raw, arrival.bytes)
        assertEquals(SenderId(5u), arrival.senderId)
    }

    @Test
    fun proto_blob_routed_to_protoReceiver_not_emitted_on_blobs() = runTest {
        val echoSignal = CompletableDeferred<Pair<SenderId, Echo>>()
        val h = Harness(backgroundScope, onEchoCb = { sid, e -> echoSignal.complete(sid to e) })
        h.coordinator.start(backgroundScope)
        runCurrent()

        val echo = Echo.newBuilder().setPingId(7).build()
        val protoFramed = ProtoEnvelope.encode(BuiltinSchemas.ECHO.toUShort(), echo.toByteArray())
        val chunk = BlobEnvelope.encode(BlobId(8u), 0, 1, protoFramed)
        h.transport.deliver(messageFrame(true, senderId = 5, seq = 2, ts = 0u, text = chunk))
        val (sid, seenEcho) = echoSignal.await()

        assertEquals(SenderId(5u), sid)
        assertEquals(7, seenEcho.pingId)
    }

    @Test
    fun message_replies_flow_through_same_pipeline() = runTest {
        val h = Harness(backgroundScope)
        h.coordinator.start(backgroundScope)
        val awaited = async { h.coordinator.incomingMessages.first() }
        runCurrent()

        h.transport.deliver(messageFrame(push = false, senderId = 1, seq = 5, ts = 0u, text = "replay".toByteArray()))
        val env = awaited.await()

        assertArrayEquals("replay".toByteArray(), (env.message as Message.Text).bytes)
    }

    @Test
    fun start_is_idempotent() = runTest {
        val h = Harness(backgroundScope)
        h.coordinator.start(backgroundScope)
        h.coordinator.start(backgroundScope)  // second call no-op
        val awaited = async { h.coordinator.incomingMessages.take(2).toList() }
        runCurrent()

        h.transport.deliver(messageFrame(true, 1, 5, 0u, "a".toByteArray()))
        h.transport.deliver(messageFrame(true, 1, 6, 0u, "b".toByteArray()))
        val emitted = awaited.await()

        // Only one copy per message despite two start() calls.
        assertEquals(2, emitted.size)
        assertEquals(2, h.msgDao.count())
    }

    @Test
    fun onConnected_sends_setClock_then_getState() = runTest {
        val h = Harness(backgroundScope)
        h.coordinator.start(backgroundScope)
        runCurrent()

        val deferred = async { h.coordinator.onConnected(1700000000u) }
        runCurrent()
        h.transport.deliver(byteArrayOf(NotificationFrame.RESP_STATE, 0))
        deferred.await()

        val cmds = h.transport.sent.map { it[0] }
        assertTrue("setClock sent", cmds.contains(0x01.toByte()))
        assertTrue("getState sent", cmds.contains(0x04.toByte()))
    }
}
