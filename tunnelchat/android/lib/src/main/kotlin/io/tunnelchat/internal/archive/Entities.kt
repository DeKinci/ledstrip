package io.tunnelchat.internal.archive

import androidx.room.ColumnInfo
import androidx.room.Entity
import androidx.room.PrimaryKey

/**
 * Message row. Composite PK (`sender_id`, `seq`) drops re-deliveries idempotently.
 * Payload encoding mirrors [io.tunnelchat.api.Message]:
 * - Text:     msgType=0x02, payload=bytes,   nodeA/nodeB null.
 * - Location: msgType=0x01, payload empty,   nodeA/nodeB set.
 * - Opaque:   msgType=other, payload=bytes,  nodeA/nodeB null.
 */
@Entity(tableName = "messages", primaryKeys = ["sender_id", "seq"])
internal data class MessageRow(
    @ColumnInfo(name = "sender_id") val senderId: Int,
    @ColumnInfo(name = "seq") val seq: Int,
    @ColumnInfo(name = "timestamp") val timestamp: Long,
    @ColumnInfo(name = "received_at_ms") val receivedAtMs: Long,
    @ColumnInfo(name = "msg_type") val msgType: Int,
    @ColumnInfo(name = "payload") val payload: ByteArray,
    @ColumnInfo(name = "node_a") val nodeA: Int?,
    @ColumnInfo(name = "node_b") val nodeB: Int?,
    @ColumnInfo(name = "delivery") val delivery: Int,
) {
    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (other !is MessageRow) return false
        return senderId == other.senderId && seq == other.seq &&
            timestamp == other.timestamp && receivedAtMs == other.receivedAtMs &&
            msgType == other.msgType && payload.contentEquals(other.payload) &&
            nodeA == other.nodeA && nodeB == other.nodeB && delivery == other.delivery
    }

    override fun hashCode(): Int {
        var h = senderId
        h = 31 * h + seq
        h = 31 * h + timestamp.hashCode()
        h = 31 * h + receivedAtMs.hashCode()
        h = 31 * h + msgType
        h = 31 * h + payload.contentHashCode()
        h = 31 * h + (nodeA ?: 0)
        h = 31 * h + (nodeB ?: 0)
        h = 31 * h + delivery
        return h
    }
}

@Entity(tableName = "presence")
internal data class PresenceRow(
    @PrimaryKey @ColumnInfo(name = "sender_id") val senderId: Int,
    @ColumnInfo(name = "presence") val presence: Int,
    @ColumnInfo(name = "last_heard_ms") val lastHeardMs: Long,
)

/** Single-row aggregate counters. Always id=0. */
@Entity(tableName = "stats")
internal data class StatsRow(
    @PrimaryKey val id: Int = 0,
    val bleFramesIn: Long = 0,
    val bleFramesOut: Long = 0,
    val bleReconnects: Long = 0,
    val textMessagesIn: Long = 0,
    val textMessagesOut: Long = 0,
    val blobChunksIn: Long = 0,
    val blobChunksOut: Long = 0,
    val blobCrcRejects: Long = 0,
    val blobPartialGcDrops: Long = 0,
    val protoIn: Long = 0,
    val protoOut: Long = 0,
    val protoUnknownSchema: Long = 0,
    val echoProbesSent: Long = 0,
)

@Entity(tableName = "logs")
internal data class LogRow(
    @PrimaryKey(autoGenerate = true) val id: Long = 0,
    @ColumnInfo(name = "ts_ms") val tsMs: Long,
    @ColumnInfo(name = "level") val level: Int,
    @ColumnInfo(name = "tag") val tag: String,
    @ColumnInfo(name = "message") val message: String,
)
