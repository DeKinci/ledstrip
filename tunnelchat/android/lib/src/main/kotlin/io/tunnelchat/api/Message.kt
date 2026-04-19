package io.tunnelchat.api

/** Opaque payload domain. Library never parses inside `Text.bytes` — blob/proto
 *  disambiguation is an implementation detail below the public API. */
sealed class Message {
    data class Text(val bytes: ByteArray) : Message() {
        override fun equals(other: Any?): Boolean =
            other is Text && bytes.contentEquals(other.bytes)
        override fun hashCode(): Int = bytes.contentHashCode()
    }
    data class Location(val nodeA: UByte, val nodeB: UByte) : Message()
    data class Opaque(val msgType: UByte, val payload: ByteArray) : Message() {
        override fun equals(other: Any?): Boolean =
            other is Opaque && msgType == other.msgType && payload.contentEquals(other.payload)
        override fun hashCode(): Int = 31 * msgType.hashCode() + payload.contentHashCode()
    }
}

data class MessageEnvelope(
    val senderId: SenderId,
    val seq: Seq,
    val timestamp: UInt,
    val receivedAtMs: Long,
    val message: Message,
    val delivery: DeliveryState,
)

/**
 * Sender-side delivery state.
 *
 * v1 transitions: [Pending] → [SentToAbonent] → [Failed].
 *
 * [MeshVisible] is reserved but unused — current firmware (`relay.cpp` `bleSendText` /
 * `bleSetLocation`) neither acks the BLE command nor loops own outbound back via
 * `IncomingMsg`, so the library has no signal to flip into this state. A firmware
 * proposal is filed in `device/retranslator/FIRMWARE_TODO.md`; when it lands, the
 * library will begin emitting [MeshVisible] without an API change.
 *
 * `BlobAcked` is deliberately absent. The mesh is broadcast + digest-sync pull; the
 * sender cannot observe receiver reassembly. Receiver-side blob completion is exposed
 * on `Tunnelchat.incomingBlobs`.
 */
enum class DeliveryState {
    Pending,
    SentToAbonent,
    MeshVisible,
    Failed,
}
