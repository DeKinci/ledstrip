package io.tunnelchat.internal.wire

import io.tunnelchat.api.SenderId
import io.tunnelchat.api.Seq
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class CommandFrameTest {

    @Test
    fun setClock_encodesCmdAndBigEndianTime() {
        val out = CommandFrame.setClock(0x01020304u)
        assertArrayEquals(
            byteArrayOf(0x01, 0x01, 0x02, 0x03, 0x04),
            out,
        )
    }

    @Test
    fun setLocation_encodesBothNodes() {
        val out = CommandFrame.setLocation(0x2Au, 0xFFu)
        assertArrayEquals(
            byteArrayOf(0x02, 0x2A, 0xFF.toByte()),
            out,
        )
    }

    @Test
    fun sendText_withinCap_prefixesLength() {
        val out = CommandFrame.sendText(byteArrayOf(0x48, 0x69))!!
        assertArrayEquals(
            byteArrayOf(0x03, 0x02, 0x48, 0x69),
            out,
        )
    }

    @Test
    fun sendText_atMaxBoundary_succeeds() {
        val payload = ByteArray(99) { 0x41 }
        val out = CommandFrame.sendText(payload)!!
        assertEquals(2 + 99, out.size)
        assertEquals(0x03.toByte(), out[0])
        assertEquals(99.toByte(), out[1])
    }

    @Test
    fun sendText_overCap_returnsNull() {
        val payload = ByteArray(100)
        assertNull(CommandFrame.sendText(payload))
    }

    @Test
    fun sendText_empty_encodesZeroLen() {
        val out = CommandFrame.sendText(byteArrayOf())!!
        assertArrayEquals(byteArrayOf(0x03, 0x00), out)
    }

    @Test
    fun getState_singleByte() {
        assertArrayEquals(byteArrayOf(0x04), CommandFrame.getState())
    }

    @Test
    fun getMessages_encodesSenderAndSeqBE() {
        val out = CommandFrame.getMessages(SenderId(0x07u), Seq(0x01ABu))
        assertArrayEquals(
            byteArrayOf(0x05, 0x07, 0x01, 0xAB.toByte()),
            out,
        )
    }

    @Test
    fun getSelfInfo_singleByte() {
        assertArrayEquals(byteArrayOf(0x06), CommandFrame.getSelfInfo())
    }
}
