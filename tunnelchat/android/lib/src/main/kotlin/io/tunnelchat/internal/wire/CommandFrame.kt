package io.tunnelchat.internal.wire

import io.tunnelchat.api.SenderId
import io.tunnelchat.api.Seq

/**
 * Encoders for BLE app→device commands. All frames are:
 *
 *   [cmd:1][payload:…]
 *
 * Source of truth: `device/retranslator/src/ble_cmd.h` +
 * `device/retranslator/PROTOCOL.md` § "App → Device".
 *
 * Pure functions returning `ByteArray`. No I/O. JVM-testable.
 */
object CommandFrame {

    const val CMD_SET_CLOCK: Byte = 0x01
    const val CMD_SET_LOCATION: Byte = 0x02
    const val CMD_SEND_TEXT: Byte = 0x03
    const val CMD_GET_STATE: Byte = 0x04
    const val CMD_GET_MESSAGES: Byte = 0x05
    const val CMD_GET_SELF_INFO: Byte = 0x06

    /** Max Text payload (firmware: MAX_MSG_PAYLOAD=100 minus 1-byte length prefix). */
    const val MAX_TEXT_BYTES: Int = 99

    fun setClock(unixSeconds: UInt): ByteArray = ByteArray(5).apply {
        this[0] = CMD_SET_CLOCK
        writeU32Be(this, 1, unixSeconds)
    }

    fun setLocation(nodeA: UByte, nodeB: UByte): ByteArray = byteArrayOf(
        CMD_SET_LOCATION,
        nodeA.toByte(),
        nodeB.toByte(),
    )

    /**
     * Build a `SendText` frame. Returns `null` if [bytes] exceeds [MAX_TEXT_BYTES] —
     * callers should surface `TunnelchatError.PayloadTooLarge(MAX_TEXT_BYTES)`.
     */
    fun sendText(bytes: ByteArray): ByteArray? {
        if (bytes.size > MAX_TEXT_BYTES) return null
        val out = ByteArray(2 + bytes.size)
        out[0] = CMD_SEND_TEXT
        out[1] = bytes.size.toByte()
        System.arraycopy(bytes, 0, out, 2, bytes.size)
        return out
    }

    fun getState(): ByteArray = byteArrayOf(CMD_GET_STATE)

    fun getMessages(sender: SenderId, fromSeq: Seq): ByteArray = ByteArray(4).apply {
        this[0] = CMD_GET_MESSAGES
        this[1] = sender.raw.toByte()
        writeU16Be(this, 2, fromSeq.raw)
    }

    fun getSelfInfo(): ByteArray = byteArrayOf(CMD_GET_SELF_INFO)
}

internal fun writeU16Be(out: ByteArray, offset: Int, value: UShort) {
    val v = value.toInt()
    out[offset] = ((v ushr 8) and 0xFF).toByte()
    out[offset + 1] = (v and 0xFF).toByte()
}

internal fun writeU32Be(out: ByteArray, offset: Int, value: UInt) {
    val v = value.toLong()
    out[offset] = ((v ushr 24) and 0xFF).toByte()
    out[offset + 1] = ((v ushr 16) and 0xFF).toByte()
    out[offset + 2] = ((v ushr 8) and 0xFF).toByte()
    out[offset + 3] = (v and 0xFF).toByte()
}

internal fun readU16Be(buf: ByteArray, offset: Int): UShort {
    val hi = buf[offset].toInt() and 0xFF
    val lo = buf[offset + 1].toInt() and 0xFF
    return ((hi shl 8) or lo).toUShort()
}

internal fun readU32Be(buf: ByteArray, offset: Int): UInt {
    val b0 = (buf[offset].toInt() and 0xFF).toLong()
    val b1 = (buf[offset + 1].toInt() and 0xFF).toLong()
    val b2 = (buf[offset + 2].toInt() and 0xFF).toLong()
    val b3 = (buf[offset + 3].toInt() and 0xFF).toLong()
    return ((b0 shl 24) or (b1 shl 16) or (b2 shl 8) or b3).toUInt()
}
