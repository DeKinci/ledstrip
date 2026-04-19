package io.tunnelchat.api

/** Abonent device ID assigned at firmware compile time (`-DDEVICE_ID=<N>`). */
@JvmInline
value class SenderId(val raw: UByte) {
    override fun toString(): String = "SenderId(0x%02x)".format(raw.toInt())
}

/** Per-sender monotonic sequence number. */
@JvmInline
value class Seq(val raw: UShort) {
    override fun toString(): String = "Seq(${raw.toInt()})"
}

/**
 * 8 raw bytes identifying a blob transfer. Chosen at random by the sender.
 * Collision probability at ~365k lifetime blobs is ~3×10⁻⁹ — negligible.
 */
@JvmInline
value class BlobId(val raw: ULong) {
    override fun toString(): String = "BlobId(0x%016x)".format(raw.toLong())
}
