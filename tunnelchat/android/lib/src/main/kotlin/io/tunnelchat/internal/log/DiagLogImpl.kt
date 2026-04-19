package io.tunnelchat.internal.log

import io.tunnelchat.api.DiagnosticLog
import io.tunnelchat.api.LogEntry
import io.tunnelchat.api.LogLevel
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.launch

/**
 * In-memory rolling log buffer with an optional disk mirror.
 *
 * [snapshot] returns a copy of the in-memory ring (newest-last) — it is synchronous,
 * matching the [DiagnosticLog] contract. [entries] is a hot [MutableSharedFlow] with
 * `replay=0`, `DROP_OLDEST` overflow, emitted under the append lock so subscribers see
 * writes in the same order as [snapshot].
 *
 * Persistence is optional. When a [Persistor] is supplied, every accepted append is
 * mirrored via [CoroutineScope.launch] and [clear] cascades; failures are swallowed
 * (diagnostic-path — must not tear down the caller).
 */
internal class DiagLogImpl(
    private val scope: CoroutineScope,
    private val persistor: Persistor? = null,
    private val maxInMemory: Int = DEFAULT_MAX_IN_MEMORY,
) : DiagnosticLog {

    interface Persistor {
        suspend fun append(entry: LogEntry)
        suspend fun clear()
    }

    private val lock = Any()
    private val ring = ArrayDeque<LogEntry>(maxInMemory)

    @Volatile private var minLevel: LogLevel = LogLevel.Verbose

    private val _entries = MutableSharedFlow<LogEntry>(
        replay = 0,
        extraBufferCapacity = 64,
        onBufferOverflow = BufferOverflow.DROP_OLDEST,
    )

    override val entries: Flow<LogEntry> = _entries.asSharedFlow()

    /** Append an entry. Filtered out if below [minLevel]. Called from library internals. */
    fun append(entry: LogEntry) {
        if (entry.level.ordinal < minLevel.ordinal) return
        synchronized(lock) {
            if (ring.size >= maxInMemory) ring.removeFirst()
            ring.addLast(entry)
            _entries.tryEmit(entry)
        }
        persistor?.let { p -> scope.launch { runCatching { p.append(entry) } } }
    }

    override fun snapshot(): List<LogEntry> = synchronized(lock) { ring.toList() }

    override fun setMinLevel(level: LogLevel) { minLevel = level }

    override fun clear() {
        synchronized(lock) { ring.clear() }
        persistor?.let { p -> scope.launch { runCatching { p.clear() } } }
    }

    companion object {
        const val DEFAULT_MAX_IN_MEMORY = 2_000
    }
}
