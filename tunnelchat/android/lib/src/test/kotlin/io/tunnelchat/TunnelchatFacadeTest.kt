package io.tunnelchat

import androidx.test.core.app.ApplicationProvider
import io.tunnelchat.api.BlobSendProgress
import io.tunnelchat.api.Message
import io.tunnelchat.api.SenderId
import io.tunnelchat.api.TunnelchatError
import io.tunnelchat.builtin.Echo
import io.tunnelchat.internal.blob.BlobEnvelope
import io.tunnelchat.internal.proto.BuiltinSchemas
import io.tunnelchat.internal.proto.ProtoEnvelope
import io.tunnelchat.internal.protocol.FakeTransport
import io.tunnelchat.internal.wire.NotificationFrame
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.async
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import java.io.File

@OptIn(ExperimentalCoroutinesApi::class)
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33])
class TunnelchatFacadeTest {

    private val ctx = ApplicationProvider.getApplicationContext<android.content.Context>()
    private val cfg = TunnelchatConfig(archivePath = File(ctx.cacheDir, "tc-test"))
    private val cfgDebug = cfg.copy(debugMode = true)
    private var tc: Tunnelchat? = null

    @After fun tearDown() { tc?.close() }

    private fun build(transport: FakeTransport, cfg: TunnelchatConfig = this.cfg): Tunnelchat =
        Tunnelchat.forTest(ctx, cfg, transport).also { tc = it }

    private fun textNotification(senderId: Int, seq: Int, text: ByteArray): ByteArray {
        val out = ByteArray(9 + 1 + text.size)
        out[0] = NotificationFrame.NOTIFY_INCOMING
        out[1] = senderId.toByte()
        out[2] = ((seq ushr 8) and 0xFF).toByte()
        out[3] = (seq and 0xFF).toByte()
        out[8] = 0x02
        out[9] = text.size.toByte()
        System.arraycopy(text, 0, out, 10, text.size)
        return out
    }

    @Test
    fun sendText_returns_envelope_and_writes_to_transport() = runTest {
        val transport = FakeTransport()
        val tc = build(transport)
        tc.connect()
        runCurrent()

        val res = tc.sendText("hello".toByteArray())
        assertTrue(res.isSuccess)
        // Locate SendText frame (0x03) — connect() may have queued setClock + getSelfInfo.
        val sendText = transport.sent.firstOrNull { it[0] == 0x03.toByte() }
        assertTrue("SendText frame written", sendText != null)
    }

    @Test
    fun sendText_oversize_surfaces_PayloadTooLarge() = runTest {
        val transport = FakeTransport()
        val tc = build(transport)
        tc.connect()
        runCurrent()

        val tooBig = ByteArray(200) { 'x'.code.toByte() }
        val res = tc.sendText(tooBig)
        assertTrue(res.isFailure)
        assertTrue(res.exceptionOrNull() is TunnelchatError.PayloadTooLarge)
    }

    @Test
    fun incoming_text_propagates_to_incomingMessages() = runTest {
        val transport = FakeTransport()
        val tc = build(transport)
        tc.connect()
        val awaited = async { tc.incomingMessages.first() }
        runCurrent()

        transport.deliver(textNotification(senderId = 7, seq = 3, text = "hi".toByteArray()))
        val env = awaited.await()

        assertEquals(SenderId(7u), env.senderId)
        assertArrayEquals("hi".toByteArray(), (env.message as Message.Text).bytes)
    }

    @Test
    fun sendProto_rejects_reserved_schemaId() = runTest {
        val transport = FakeTransport()
        val tc = build(transport)
        tc.connect()
        runCurrent()

        val echo = Echo.newBuilder().setPingId(1).build()
        var threw = false
        try {
            tc.sendProto(BuiltinSchemas.ECHO.toUShort(), echo)
        } catch (_: IllegalArgumentException) {
            threw = true
        }
        assertTrue("reserved schemaId must throw", threw)
    }

    @Test
    fun sendBlob_transmits_chunks_through_transport() = runTest {
        val transport = FakeTransport()
        val tc = build(transport)
        tc.connect()
        runCurrent()

        val handle = tc.sendBlob("hello world".toByteArray())
        // Wait for terminal state.
        val terminal = handle.progress.first {
            it is BlobSendProgress.Transmitted || it is BlobSendProgress.Failed
        }
        assertTrue(terminal is BlobSendProgress.Transmitted)
        // At least one SendText frame containing a blob marker.
        val blobFrames = transport.sent.filter {
            it.size > 5 && it[0] == 0x03.toByte() // SendText, payload follows length byte
        }
        assertTrue("blob chunk transmitted via SendText", blobFrames.isNotEmpty())
    }

    @Test
    fun echoProbe_returns_Timeout_when_debugMode_off() = runTest {
        val transport = FakeTransport()
        val tc = build(transport, cfg)
        tc.connect()
        runCurrent()

        val res = tc.echoProbe(SenderId(1u))
        assertTrue(res.isFailure)
        assertTrue(res.exceptionOrNull() is TunnelchatError.Timeout)
    }

    @Test
    fun proto_arrival_surfaces_on_incomingProtos() = runTest {
        val transport = FakeTransport()
        val tc = build(transport)
        tc.connect()
        runCurrent()

        // Register an app schema (id >= 256). Reuse Echo as the message type just for parsing.
        tc.protoRegistry.register(300u, Echo.parser())
        val awaited = async { tc.incomingProtos.first() }
        runCurrent()

        val echo = Echo.newBuilder().setPingId(42).build()
        val protoFramed = ProtoEnvelope.encode(300u, echo.toByteArray())
        val chunk = BlobEnvelope.encode(io.tunnelchat.api.BlobId(99u), 0, 1, protoFramed)
        transport.deliver(textNotification(senderId = 5, seq = 1, text = chunk))
        val arrival = awaited.await()

        assertEquals(SenderId(5u), arrival.senderId)
        assertEquals(300u.toUShort(), arrival.schemaId)
        advanceUntilIdle()
    }
}
