package io.tunnelchat.internal.archive

import androidx.test.core.app.ApplicationProvider
import io.tunnelchat.api.DeliveryState
import io.tunnelchat.api.Message
import io.tunnelchat.api.MessageEnvelope
import io.tunnelchat.api.SenderId
import io.tunnelchat.api.Seq
import kotlinx.coroutines.test.runTest
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33])
class MessageStoreTest {
    private lateinit var db: TunnelchatDatabase
    private lateinit var store: MessageStore

    @Before
    fun setUp() {
        db = TunnelchatDatabase.inMemory(ApplicationProvider.getApplicationContext())
        store = MessageStore(db.messages())
    }

    @After
    fun tearDown() {
        db.close()
    }

    private fun env(sender: Int, seq: Int, msg: Message = Message.Text("hi".toByteArray())) =
        MessageEnvelope(
            senderId = SenderId(sender.toUByte()),
            seq = Seq(seq.toUShort()),
            timestamp = 1000u,
            receivedAtMs = 12345L,
            message = msg,
            delivery = DeliveryState.SentToAbonent,
        )

    @Test
    fun insert_isIdempotentOnSameSenderSeq() = runTest {
        assertTrue(store.insert(env(1, 10)))
        assertFalse(store.insert(env(1, 10)))
        assertEquals(1, store.count())
    }

    @Test
    fun highSeq_returnsMaxPerSender() = runTest {
        store.insert(env(1, 5))
        store.insert(env(1, 100))
        store.insert(env(1, 42))
        store.insert(env(2, 9999))
        assertEquals(Seq(100u), store.highSeq(SenderId(1u)))
        assertEquals(Seq(9999u), store.highSeq(SenderId(2u)))
        assertNull(store.highSeq(SenderId(99u)))
    }

    @Test
    fun range_returnsInclusiveAscending() = runTest {
        for (s in listOf(1, 3, 5, 7, 9)) store.insert(env(1, s))
        val out = store.range(SenderId(1u), Seq(3u), Seq(7u))
        assertEquals(listOf(3, 5, 7), out.map { it.seq.raw.toInt() })
    }

    @Test
    fun roundTrip_preservesAllMessageTypes() = runTest {
        val text = env(1, 1, Message.Text(byteArrayOf(0xDE.toByte(), 0xAD.toByte())))
        val loc = env(1, 2, Message.Location(0x10u, 0x20u))
        val opa = env(1, 3, Message.Opaque(0x55u, byteArrayOf(1, 2, 3)))
        listOf(text, loc, opa).forEach { store.insert(it) }
        val out = store.range(SenderId(1u), Seq(1u), Seq(3u))
        assertEquals(text, out[0])
        assertEquals(loc, out[1])
        assertEquals(opa, out[2])
    }

    @Test
    fun highSeq_handlesUShortNearMax() = runTest {
        store.insert(env(1, 0xFFFE))
        store.insert(env(1, 0x0001))
        // Numeric MAX wins; wrap-around handling is sender-side concern, not store.
        assertEquals(Seq(0xFFFEu), store.highSeq(SenderId(1u)))
    }
}
