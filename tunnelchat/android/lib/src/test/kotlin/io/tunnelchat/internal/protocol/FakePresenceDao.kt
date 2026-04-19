package io.tunnelchat.internal.protocol

import io.tunnelchat.internal.archive.PresenceDao
import io.tunnelchat.internal.archive.PresenceRow

internal class FakePresenceDao : PresenceDao {
    private val rows = LinkedHashMap<Int, PresenceRow>()

    override suspend fun upsert(row: PresenceRow) { rows[row.senderId] = row }
    override suspend fun all(): List<PresenceRow> = rows.values.toList()
    override suspend fun get(senderId: Int): PresenceRow? = rows[senderId]
    override suspend fun clear() { rows.clear() }
}
