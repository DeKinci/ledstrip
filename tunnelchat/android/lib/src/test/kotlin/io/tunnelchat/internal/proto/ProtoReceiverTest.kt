package io.tunnelchat.internal.proto

import io.tunnelchat.api.ProtoArrival
import io.tunnelchat.api.SenderId
import io.tunnelchat.builtin.Echo
import io.tunnelchat.builtin.Pong
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.launchIn
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class ProtoReceiverTest {

    private val sender = SenderId(7u)

    private fun recv(
        onEcho: suspend (SenderId, Echo) -> Unit = { _, _ -> },
        onPong: (SenderId, Pong) -> Unit = { _, _ -> },
        registry: ProtoRegistryImpl = ProtoRegistryImpl(),
    ) = ProtoReceiver(registry, onEcho, onPong)

    @Test
    fun non_proto_bytes_returns_false() = runTest {
        val r = recv()
        val arrivals = mutableListOf<ProtoArrival>()
        val job = r.arrivals.onEach { arrivals += it }.launchIn(this); runCurrent()
        assertFalse(r.accept(sender, byteArrayOf(0x01, 0x02, 0x03)))
        advanceUntilIdle()
        assertTrue(arrivals.isEmpty())
        job.cancel()
    }

    @Test
    fun known_app_schema_emits_known_arrival() = runTest {
        val registry = ProtoRegistryImpl()
        registry.register(300u, Echo.parser())
        val r = recv(registry = registry)
        val arrivals = mutableListOf<ProtoArrival>()
        val job = r.arrivals.onEach { arrivals += it }.launchIn(this); runCurrent()

        val msg = Echo.newBuilder().setPingId(42).setOriginTs(1234).build()
        val framed = ProtoEnvelope.encode(300u, msg.toByteArray())
        assertTrue(r.accept(sender, framed))
        advanceUntilIdle()

        assertEquals(1, arrivals.size)
        val a = arrivals[0] as ProtoArrival.Known
        assertEquals(300u.toUShort(), a.schemaId)
        assertEquals(sender, a.senderId)
        assertEquals(42, (a.message as Echo).pingId)
        job.cancel()
    }

    @Test
    fun unknown_schema_emits_unknown_arrival() = runTest {
        val r = recv()
        val arrivals = mutableListOf<ProtoArrival>()
        val job = r.arrivals.onEach { arrivals += it }.launchIn(this); runCurrent()

        val payload = byteArrayOf(9, 8, 7)
        assertTrue(r.accept(sender, ProtoEnvelope.encode(999u, payload)))
        advanceUntilIdle()

        assertEquals(1, arrivals.size)
        val a = arrivals[0] as ProtoArrival.Unknown
        assertEquals(999u.toUShort(), a.schemaId)
        assertArrayEquals(payload, a.rawBytes)
        job.cancel()
    }

    @Test
    fun echo_triggers_onEcho_and_does_not_emit() = runTest {
        var seen: Pair<SenderId, Echo>? = null
        val r = recv(onEcho = { sid, e -> seen = sid to e })
        val arrivals = mutableListOf<ProtoArrival>()
        val job = r.arrivals.onEach { arrivals += it }.launchIn(this); runCurrent()

        val echo = Echo.newBuilder().setPingId(1).setOriginTs(100).build()
        val framed = ProtoEnvelope.encode(BuiltinSchemas.ECHO.toUShort(), echo.toByteArray())
        assertTrue(r.accept(sender, framed))
        advanceUntilIdle()

        assertEquals(sender, seen!!.first)
        assertEquals(1, seen!!.second.pingId)
        assertTrue(arrivals.isEmpty())
        job.cancel()
    }

    @Test
    fun pong_triggers_onPong_and_does_not_emit() = runTest {
        var seen: Pair<SenderId, Pong>? = null
        val r = recv(onPong = { sid, p -> seen = sid to p })
        val arrivals = mutableListOf<ProtoArrival>()
        val job = r.arrivals.onEach { arrivals += it }.launchIn(this); runCurrent()

        val pong = Pong.newBuilder().setPingId(7).setOriginTs(100).build()
        val framed = ProtoEnvelope.encode(BuiltinSchemas.PONG.toUShort(), pong.toByteArray())
        assertTrue(r.accept(sender, framed))
        advanceUntilIdle()

        assertEquals(7, seen!!.second.pingId)
        assertTrue(arrivals.isEmpty())
        job.cancel()
    }

    @Test
    fun malformed_proto_for_known_schema_surfaces_unknown() = runTest {
        val registry = ProtoRegistryImpl()
        registry.register(400u, Echo.parser())
        val r = recv(registry = registry)
        val arrivals = mutableListOf<ProtoArrival>()
        val job = r.arrivals.onEach { arrivals += it }.launchIn(this); runCurrent()

        // Random bytes that won't parse as an Echo.
        val garbage = byteArrayOf(0xFF.toByte(), 0xFF.toByte(), 0xFF.toByte(), 0xFF.toByte())
        assertTrue(r.accept(sender, ProtoEnvelope.encode(400u, garbage)))
        advanceUntilIdle()

        assertEquals(1, arrivals.size)
        assertTrue(arrivals[0] is ProtoArrival.Unknown)
        job.cancel()
    }
}
