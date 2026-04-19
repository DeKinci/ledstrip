package io.tunnelchat.internal.archive

import io.tunnelchat.api.LogEntry
import io.tunnelchat.api.LogLevel

/**
 * Rolling DB-backed log buffer. Trims to [maxEntries] newest rows on each append
 * once the table grows past the cap.
 */
internal class LogStore(
    private val dao: LogDao,
    private val maxEntries: Int = DEFAULT_MAX,
) {
    suspend fun append(entry: LogEntry) {
        dao.append(LogRow(tsMs = entry.tsMs, level = entry.level.ordinal, tag = entry.tag, message = entry.message))
        val n = dao.count()
        if (n > maxEntries) dao.trimOldest(n - maxEntries)
    }

    suspend fun snapshot(): List<LogEntry> = dao.all().map { it.toEntry() }

    suspend fun count(): Int = dao.count()

    suspend fun clear() = dao.clear()

    companion object {
        const val DEFAULT_MAX = 2_000
    }
}

private fun LogRow.toEntry() = LogEntry(
    tsMs = tsMs,
    level = LogLevel.values()[level],
    tag = tag,
    message = message,
)
