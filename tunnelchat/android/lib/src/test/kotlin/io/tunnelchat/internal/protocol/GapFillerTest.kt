package io.tunnelchat.internal.protocol

import io.tunnelchat.api.DeliveryState
import io.tunnelchat.api.Message
import io.tunnelchat.api.MessageEnvelope
import io.tunnelchat.api.Presence
import io.tunnelchat.api.SenderId
import io.tunnelchat.api.Seq
import io.tunnelchat.internal.archive.MessageStore
import io.tunnelchat.internal.wire.CommandFrame
import io.tunnelchat.internal.wire.NotificationFrame
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.async
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class GapFillerTest {

    private class Harness(scope: kotlinx.coroutines.CoroutineScope) {
        val transport = FakeTransport()
        val dispatcher = CommandDispatcher(transport).also { it.start(scope) }
        val dao = FakeMessageDao()
        val store = MessageStore(dao)
        val gapFiller = GapFiller(dispatcher, store)
    }

    private fun stateRespFrame(vararg entries: IntArray): ByteArray {
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

    private fun entry(senderId: Int, high: Int, loc: Int = 0, nodeA: Int = 0, nodeB: Int = 0, presence: Int = 0): IntArray =
        intArrayOf(
            senderId,
            (high ushr 8) and 0xFF, high and 0xFF,
            (loc ushr 8) and 0xFF, loc and 0xFF,
            nodeA, nodeB, presence,
        )

    private suspend fun seed(store: MessageStore, senderId: Int, seq: Int) {
        store.insert(
            MessageEnvelope(
                senderId = SenderId(senderId.toUByte()),
                seq = Seq(seq.toUShort()),
                timestamp = 0u,
                receivedAtMs = 0L,
                message = Message.Text("x".toByteArray()),
                delivery = DeliveryState.Pending,
            )
        )
    }

    @Test
    fun issues_get_messages_for_sender_with_gap() = runTest {
        val h = Harness(backgroundScope)
        seed(h.store, senderId = 1, seq = 5)
        val deferred = async { h.gapFiller.fillGaps() }
        runCurrent()
        h.transport.deliver(stateRespFrame(entry(senderId = 1, high = 10)))
        val result = deferred.await()

        assertTrue(result.isSuccess)
        val issued = result.getOrThrow()
        assertEquals(1, issued.size)
        assertEquals(SenderId(1u), issued[0].senderId)
        assertEquals(Seq(6u), issued[0].fromSeq)

        // GetState + one GetMessages were written.
        val cmds = h.transport.sent.map { it[0] }
        assertEquals(listOf(CommandFrame.CMD_GET_STATE, CommandFrame.CMD_GET_MESSAGES), cmds)
    }

    @Test
    fun no_get_messages_when_up_to_date() = runTest {
        val h = Harness(backgroundScope)
        seed(h.store, senderId = 1, seq = 10)
        val deferred = async { h.gapFiller.fillGaps() }
        runCurrent()
        h.transport.deliver(stateRespFrame(entry(senderId = 1, high = 10)))
        val result = deferred.await()

        assertTrue(result.isSuccess)
        assertTrue(result.getOrThrow().isEmpty())
        assertFalse(h.transport.sent.any { it[0] == CommandFrame.CMD_GET_MESSAGES })
    }

    @Test
    fun requests_from_zero_when_no_local_history() = runTest {
        val h = Harness(backgroundScope)
        val deferred = async { h.gapFiller.fillGaps() }
        runCurrent()
        h.transport.deliver(stateRespFrame(entry(senderId = 2, high = 3)))
        val result = deferred.await()

        val issued = result.getOrThrow()
        assertEquals(1, issued.size)
        assertEquals(SenderId(2u), issued[0].senderId)
        assertEquals(Seq(0u), issued[0].fromSeq)
    }

    @Test
    fun handles_multiple_senders_independently() = runTest {
        val h = Harness(backgroundScope)
        seed(h.store, senderId = 1, seq = 5)  // needs 6..7
        seed(h.store, senderId = 2, seq = 9)  // up to date
        val deferred = async { h.gapFiller.fillGaps() }
        runCurrent()
        h.transport.deliver(stateRespFrame(
            entry(senderId = 1, high = 7),
            entry(senderId = 2, high = 9),
            entry(senderId = 3, high = 4),
        ))
        val issued = deferred.await().getOrThrow()
        val byId = issued.associateBy { it.senderId }
        assertEquals(2, issued.size)
        assertEquals(Seq(6u), byId[SenderId(1u)]!!.fromSeq)
        assertEquals(Seq(0u), byId[SenderId(3u)]!!.fromSeq)
        assertFalse(byId.containsKey(SenderId(2u)))
    }

    @Test
    fun getState_failure_bubbles_up() = runTest {
        val h = Harness(backgroundScope)
        h.transport.acceptSends = false
        val result = h.gapFiller.fillGaps()
        assertTrue(result.isFailure)
    }
}
