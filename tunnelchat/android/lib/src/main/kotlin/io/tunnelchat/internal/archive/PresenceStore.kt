package io.tunnelchat.internal.archive

import io.tunnelchat.api.Presence
import io.tunnelchat.api.SenderId

internal data class PresenceSnapshot(
    val sender: SenderId,
    val presence: Presence,
    val lastHeardMs: Long,
)

internal class PresenceStore(private val dao: PresenceDao) {
    suspend fun upsert(sender: SenderId, presence: Presence, lastHeardMs: Long) {
        dao.upsert(PresenceRow(sender.raw.toInt(), presence.ordinal, lastHeardMs))
    }

    suspend fun get(sender: SenderId): PresenceSnapshot? =
        dao.get(sender.raw.toInt())?.toSnapshot()

    suspend fun all(): List<PresenceSnapshot> = dao.all().map { it.toSnapshot() }

    suspend fun clear() = dao.clear()
}

private fun PresenceRow.toSnapshot() = PresenceSnapshot(
    sender = SenderId(senderId.toUByte()),
    presence = Presence.values()[presence],
    lastHeardMs = lastHeardMs,
)
