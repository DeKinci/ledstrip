package io.tunnelchat.internal.proto

/**
 * Envelope framing for proto-over-blob.
 *
 * Wire layout (inside a completed blob payload):
 *
 *   [MAGIC:1=0xA7][schema_id:2 BE][proto_bytes:N]
 *
 * The 3-byte header disambiguates a proto payload from a raw-bytes blob at the
 * receiver; app code that calls [io.tunnelchat.Tunnelchat.sendBlob] cannot produce
 * a payload that collides with this, as the facade strips proto routing before
 * handing bytes to the blob sender.
 */
internal object ProtoEnvelope {
    const val MAGIC: Byte = 0xA7.toByte()
    const val HEADER_BYTES: Int = 3

    data class Decoded(val schemaId: UShort, val payload: ByteArray) {
        override fun equals(other: Any?): Boolean =
            other is Decoded && schemaId == other.schemaId && payload.contentEquals(other.payload)
        override fun hashCode(): Int = 31 * schemaId.hashCode() + payload.contentHashCode()
    }

    fun encode(schemaId: UShort, payload: ByteArray): ByteArray {
        val out = ByteArray(HEADER_BYTES + payload.size)
        out[0] = MAGIC
        out[1] = ((schemaId.toInt() ushr 8) and 0xFF).toByte()
        out[2] = (schemaId.toInt() and 0xFF).toByte()
        System.arraycopy(payload, 0, out, HEADER_BYTES, payload.size)
        return out
    }

    /** Returns `null` if [bytes] is not a proto envelope. Empty proto bodies are allowed. */
    fun tryDecode(bytes: ByteArray): Decoded? {
        if (bytes.size < HEADER_BYTES) return null
        if (bytes[0] != MAGIC) return null
        val schemaId = (((bytes[1].toInt() and 0xFF) shl 8) or (bytes[2].toInt() and 0xFF)).toUShort()
        val payload = bytes.copyOfRange(HEADER_BYTES, bytes.size)
        return Decoded(schemaId, payload)
    }
}
