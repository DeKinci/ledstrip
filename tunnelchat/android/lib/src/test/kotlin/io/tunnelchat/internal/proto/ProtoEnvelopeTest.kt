package io.tunnelchat.internal.proto

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class ProtoEnvelopeTest {

    @Test
    fun round_trip_preserves_schema_and_payload() {
        val payload = byteArrayOf(1, 2, 3, 4, 5)
        val framed = ProtoEnvelope.encode(schemaId = 0x1234u, payload = payload)
        assertEquals(ProtoEnvelope.MAGIC, framed[0])
        val decoded = ProtoEnvelope.tryDecode(framed)!!
        assertEquals(0x1234u.toUShort(), decoded.schemaId)
        assertArrayEquals(payload, decoded.payload)
    }

    @Test
    fun empty_payload_round_trips() {
        val framed = ProtoEnvelope.encode(schemaId = 1u, payload = ByteArray(0))
        val decoded = ProtoEnvelope.tryDecode(framed)!!
        assertEquals(1u.toUShort(), decoded.schemaId)
        assertEquals(0, decoded.payload.size)
    }

    @Test
    fun max_schema_id_round_trips() {
        val framed = ProtoEnvelope.encode(schemaId = 0xFFFFu, payload = byteArrayOf(0x42))
        val decoded = ProtoEnvelope.tryDecode(framed)!!
        assertEquals(0xFFFFu.toUShort(), decoded.schemaId)
        assertArrayEquals(byteArrayOf(0x42), decoded.payload)
    }

    @Test
    fun wrong_magic_rejected() {
        val bytes = byteArrayOf(0x00, 0x00, 0x01, 0x02, 0x03)
        assertNull(ProtoEnvelope.tryDecode(bytes))
    }

    @Test
    fun too_short_rejected() {
        assertNull(ProtoEnvelope.tryDecode(ByteArray(0)))
        assertNull(ProtoEnvelope.tryDecode(byteArrayOf(ProtoEnvelope.MAGIC)))
        assertNull(ProtoEnvelope.tryDecode(byteArrayOf(ProtoEnvelope.MAGIC, 0x01)))
    }
}
