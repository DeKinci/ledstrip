package io.tunnelchat.internal.protocol

import io.tunnelchat.internal.archive.MessageDao
import io.tunnelchat.internal.archive.MessageRow

/** In-memory [MessageDao] fake that mirrors the Room IGNORE-on-conflict semantics. */
internal class FakeMessageDao : MessageDao {
    private data class Key(val senderId: Int, val seq: Int)
    private val rows = LinkedHashMap<Key, MessageRow>()

    override suspend fun insert(row: MessageRow): Long {
        val key = Key(row.senderId, row.seq)
        if (rows.containsKey(key)) return -1L
        rows[key] = row
        return rows.size.toLong()
    }

    override suspend fun highSeq(senderId: Int): Int? =
        rows.values.filter { it.senderId == senderId }.maxOfOrNull { it.seq }

    override suspend fun range(senderId: Int, fromSeq: Int, toSeq: Int): List<MessageRow> =
        rows.values
            .filter { it.senderId == senderId && it.seq in fromSeq..toSeq }
            .sortedBy { it.seq }

    override suspend fun count(): Int = rows.size

    override suspend fun clear() { rows.clear() }
}
