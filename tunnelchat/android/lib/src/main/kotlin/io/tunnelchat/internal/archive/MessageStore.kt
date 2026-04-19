package io.tunnelchat.internal.archive

import io.tunnelchat.api.DeliveryState
import io.tunnelchat.api.Message
import io.tunnelchat.api.MessageEnvelope
import io.tunnelchat.api.SenderId
import io.tunnelchat.api.Seq

internal class MessageStore(private val dao: MessageDao) {
    /** Returns true if the row was new; false if (sender_id, seq) already existed. */
    suspend fun insert(env: MessageEnvelope): Boolean =
        dao.insert(env.toRow()) != -1L

    suspend fun highSeq(sender: SenderId): Seq? =
        dao.highSeq(sender.raw.toInt())?.let { Seq(it.toUShort()) }

    suspend fun range(sender: SenderId, fromSeq: Seq, toSeq: Seq): List<MessageEnvelope> =
        dao.range(sender.raw.toInt(), fromSeq.raw.toInt(), toSeq.raw.toInt())
            .map { it.toEnvelope() }

    suspend fun count(): Int = dao.count()
    suspend fun clear() = dao.clear()
}

internal const val MSG_TYPE_LOCATION = 0x01
internal const val MSG_TYPE_TEXT = 0x02

internal fun MessageEnvelope.toRow(): MessageRow {
    val (msgType, payload, nodeA, nodeB) = when (val m = message) {
        is Message.Text -> Quad(MSG_TYPE_TEXT, m.bytes, null, null)
        is Message.Location -> Quad(MSG_TYPE_LOCATION, EMPTY, m.nodeA.toInt(), m.nodeB.toInt())
        is Message.Opaque -> Quad(m.msgType.toInt(), m.payload, null, null)
    }
    return MessageRow(
        senderId = senderId.raw.toInt(),
        seq = seq.raw.toInt(),
        timestamp = timestamp.toLong(),
        receivedAtMs = receivedAtMs,
        msgType = msgType,
        payload = payload,
        nodeA = nodeA,
        nodeB = nodeB,
        delivery = delivery.ordinal,
    )
}

internal fun MessageRow.toEnvelope(): MessageEnvelope {
    val msg: Message = when (msgType) {
        MSG_TYPE_TEXT -> Message.Text(payload)
        MSG_TYPE_LOCATION -> Message.Location(
            (nodeA ?: 0).toUByte(),
            (nodeB ?: 0).toUByte(),
        )
        else -> Message.Opaque(msgType.toUByte(), payload)
    }
    return MessageEnvelope(
        senderId = SenderId(senderId.toUByte()),
        seq = Seq(seq.toUShort()),
        timestamp = timestamp.toUInt(),
        receivedAtMs = receivedAtMs,
        message = msg,
        delivery = DeliveryState.values()[delivery],
    )
}

private val EMPTY = ByteArray(0)
private data class Quad(val a: Int, val b: ByteArray, val c: Int?, val d: Int?)
