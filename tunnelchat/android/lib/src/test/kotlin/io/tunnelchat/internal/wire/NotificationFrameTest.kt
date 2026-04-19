package io.tunnelchat.internal.wire

import io.tunnelchat.api.Message
import io.tunnelchat.api.Presence
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class NotificationFrameTest {

    @Test
    fun empty_isMalformed() {
        assertTrue(NotificationFrame.decode(byteArrayOf()) is Notification.Malformed)
    }

    @Test
    fun unknownResp_isMalformed() {
        assertTrue(NotificationFrame.decode(byteArrayOf(0x77)) is Notification.Malformed)
    }

    // ── StateResp (0x80) ─────────────────────────────────────────────────────

    @Test
    fun stateResp_zeroEntries() {
        val n = NotificationFrame.decode(byteArrayOf(0x80.toByte(), 0x00)) as Notification.StateResp
        assertTrue(n.entries.isEmpty())
    }

    @Test
    fun stateResp_oneEntry() {
        val frame = byteArrayOf(
            0x80.toByte(), 0x01,
            // senderId=0x05, highSeq=0x00FE, locSeq=0x00AA, nodeA=0x01, nodeB=0x02, presence=1(stale)
            0x05, 0x00, 0xFE.toByte(), 0x00, 0xAA.toByte(), 0x01, 0x02, 0x01,
        )
        val n = NotificationFrame.decode(frame) as Notification.StateResp
        assertEquals(1, n.entries.size)
        val e = n.entries[0]
        assertEquals(0x05u.toUByte(), e.senderId.raw)
        assertEquals(0x00FEu.toUShort(), e.highSeq.raw)
        assertEquals(0x00AAu.toUShort(), e.locSeq.raw)
        assertEquals(0x01u.toUByte(), e.nodeA)
        assertEquals(0x02u.toUByte(), e.nodeB)
        assertEquals(Presence.Stale, e.presence)
    }

    @Test
    fun stateResp_truncated_isMalformed() {
        val frame = byteArrayOf(0x80.toByte(), 0x01, 0x05, 0x00) // claims 1 entry, truncated
        assertTrue(NotificationFrame.decode(frame) is Notification.Malformed)
    }

    @Test
    fun stateResp_badPresenceByte_isMalformed() {
        val frame = byteArrayOf(
            0x80.toByte(), 0x01,
            0x05, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x09, // presence=9 invalid
        )
        assertTrue(NotificationFrame.decode(frame) is Notification.Malformed)
    }

    // ── MessageResp / IncomingMsg (0x81 / 0x82) ──────────────────────────────

    @Test
    fun incomingMsg_text() {
        // [0x82][sid=0x03][seq=0x0007][ts=0x11223344][type=0x02][len=0x03]"Hi!"
        val frame = byteArrayOf(
            0x82.toByte(), 0x03, 0x00, 0x07,
            0x11, 0x22, 0x33, 0x44,
            0x02, 0x03, 'H'.code.toByte(), 'i'.code.toByte(), '!'.code.toByte(),
        )
        val n = NotificationFrame.decode(frame) as Notification.MessageFrame
        assertTrue(n.push)
        assertEquals(0x03u.toUByte(), n.senderId.raw)
        assertEquals(0x0007u.toUShort(), n.seq.raw)
        assertEquals(0x11223344u, n.timestamp)
        val text = n.message as Message.Text
        assertEquals("Hi!", String(text.bytes))
    }

    @Test
    fun messageResp_location() {
        val frame = byteArrayOf(
            0x81.toByte(), 0x04, 0x00, 0x01,
            0x00, 0x00, 0x00, 0x00,
            0x01, 0x0A, 0x0B,
        )
        val n = NotificationFrame.decode(frame) as Notification.MessageFrame
        assertTrue(!n.push)
        val loc = n.message as Message.Location
        assertEquals(0x0Au.toUByte(), loc.nodeA)
        assertEquals(0x0Bu.toUByte(), loc.nodeB)
    }

    @Test
    fun message_unknownType_isOpaque() {
        val frame = byteArrayOf(
            0x82.toByte(), 0x04, 0x00, 0x01,
            0x00, 0x00, 0x00, 0x00,
            0x77, 0x01, 0x02, 0x03,
        )
        val n = NotificationFrame.decode(frame) as Notification.MessageFrame
        val op = n.message as Message.Opaque
        assertEquals(0x77u.toUByte(), op.msgType)
        assertEquals(3, op.payload.size)
    }

    @Test
    fun message_truncated_isMalformed() {
        val frame = byteArrayOf(0x82.toByte(), 0x01, 0x00, 0x01, 0x00, 0x00) // header short
        assertTrue(NotificationFrame.decode(frame) is Notification.Malformed)
    }

    @Test
    fun message_textLengthBeyondBuffer_isMalformed() {
        // Claims len=0x10 but payload after length byte is only 2 bytes.
        val frame = byteArrayOf(
            0x82.toByte(), 0x01, 0x00, 0x01,
            0x00, 0x00, 0x00, 0x00,
            0x02, 0x10, 0x41, 0x42,
        )
        assertTrue(NotificationFrame.decode(frame) is Notification.Malformed)
    }

    // ── PresenceEvent (0x83) ─────────────────────────────────────────────────

    @Test
    fun presence_online() {
        val n = NotificationFrame.decode(byteArrayOf(0x83.toByte(), 0x05, 0x00)) as Notification.PresenceFrame
        assertEquals(Presence.Online, n.presence)
        assertEquals(0x05u.toUByte(), n.senderId.raw)
    }

    @Test
    fun presence_offline() {
        val n = NotificationFrame.decode(byteArrayOf(0x83.toByte(), 0x05, 0x02)) as Notification.PresenceFrame
        assertEquals(Presence.Offline, n.presence)
    }

    @Test
    fun presence_bad_isMalformed() {
        assertTrue(NotificationFrame.decode(byteArrayOf(0x83.toByte(), 0x05, 0x05)) is Notification.Malformed)
    }

    @Test
    fun presence_tooShort_isMalformed() {
        assertTrue(NotificationFrame.decode(byteArrayOf(0x83.toByte(), 0x05)) is Notification.Malformed)
    }

    // ── SelfInfoResp (0x84) ──────────────────────────────────────────────────

    @Test
    fun selfInfoResp_decodes() {
        // [0x84][deviceId=0x07][clock=0x11223344][activeSenders=0x03][bootCount=0x00000005]
        val frame = byteArrayOf(
            0x84.toByte(),
            0x07,
            0x11, 0x22, 0x33, 0x44,
            0x03,
            0x00, 0x00, 0x00, 0x05,
        )
        val n = NotificationFrame.decode(frame) as Notification.SelfInfoResp
        assertEquals(0x07u.toUByte(), n.deviceId)
        assertEquals(0x11223344u, n.clockUnix)
        assertEquals(0x03u.toUByte(), n.activeSenders)
        assertEquals(5u, n.bootCount)
    }

    @Test
    fun selfInfoResp_tooShort_isMalformed() {
        val frame = byteArrayOf(0x84.toByte(), 0x07, 0x11, 0x22, 0x33, 0x44, 0x03)
        assertTrue(NotificationFrame.decode(frame) is Notification.Malformed)
    }
}
