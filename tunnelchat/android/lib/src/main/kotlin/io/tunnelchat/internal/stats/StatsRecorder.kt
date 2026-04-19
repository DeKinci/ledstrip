package io.tunnelchat.internal.stats

import io.tunnelchat.api.Statistics
import io.tunnelchat.internal.archive.StatsStore
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * In-memory counter + histogram store. Produces immutable [Statistics] snapshots via
 * [state]. All mutators are thread-safe under a single monitor; updates publish a new
 * snapshot on every change.
 *
 * Persistence is caller-driven: [flush] writes the current snapshot to [StatsStore];
 * [loadFrom] seeds counters from disk (RTT histogram is not persisted and is zeroed).
 */
internal class StatsRecorder(
    private val rttCap: Int = DEFAULT_RTT_CAP,
) {
    private val lock = Any()

    // Raw counter fields — mutated only under [lock].
    private var bleFramesIn = 0L
    private var bleFramesOut = 0L
    private var bleReconnects = 0L
    private var textMessagesIn = 0L
    private var textMessagesOut = 0L
    private var blobChunksIn = 0L
    private var blobChunksOut = 0L
    private var blobCrcRejects = 0L
    private var blobPartialGcDrops = 0L
    private var protoIn = 0L
    private var protoOut = 0L
    private var protoUnknownSchema = 0L
    private var echoProbesSent = 0L

    // Rolling RTT sample ring. Sized at [rttCap]; oldest overwritten.
    private val rttSamples = LongArray(rttCap)
    private var rttSize = 0
    private var rttNext = 0

    private val _state = MutableStateFlow(snapshotLocked())
    val state: StateFlow<Statistics> = _state.asStateFlow()

    fun snapshot(): Statistics = synchronized(lock) { snapshotLocked() }

    // ── Counter mutators ─────────────────────────────────────────────────────

    fun incBleFramesIn(n: Long = 1) = bump { bleFramesIn += n }
    fun incBleFramesOut(n: Long = 1) = bump { bleFramesOut += n }
    fun incBleReconnects(n: Long = 1) = bump { bleReconnects += n }
    fun incTextMessagesIn(n: Long = 1) = bump { textMessagesIn += n }
    fun incTextMessagesOut(n: Long = 1) = bump { textMessagesOut += n }
    fun incBlobChunksIn(n: Long = 1) = bump { blobChunksIn += n }
    fun incBlobChunksOut(n: Long = 1) = bump { blobChunksOut += n }
    fun incBlobCrcRejects(n: Long = 1) = bump { blobCrcRejects += n }
    fun incBlobPartialGcDrops(n: Long = 1) = bump { blobPartialGcDrops += n }
    fun incProtoIn(n: Long = 1) = bump { protoIn += n }
    fun incProtoOut(n: Long = 1) = bump { protoOut += n }
    fun incProtoUnknownSchema(n: Long = 1) = bump { protoUnknownSchema += n }
    fun incEchoProbesSent(n: Long = 1) = bump { echoProbesSent += n }

    fun recordEchoRtt(rttMs: Long) {
        synchronized(lock) {
            rttSamples[rttNext] = rttMs
            rttNext = (rttNext + 1) % rttCap
            if (rttSize < rttCap) rttSize++
            _state.value = snapshotLocked()
        }
    }

    fun reset() {
        synchronized(lock) {
            bleFramesIn = 0; bleFramesOut = 0; bleReconnects = 0
            textMessagesIn = 0; textMessagesOut = 0
            blobChunksIn = 0; blobChunksOut = 0; blobCrcRejects = 0; blobPartialGcDrops = 0
            protoIn = 0; protoOut = 0; protoUnknownSchema = 0
            echoProbesSent = 0
            rttSize = 0; rttNext = 0
            _state.value = snapshotLocked()
        }
    }

    // ── Persistence ──────────────────────────────────────────────────────────

    suspend fun flush(store: StatsStore) = store.flush(snapshot())

    /** Seed counters from disk. RTT histogram is not persisted and stays zeroed. */
    suspend fun loadFrom(store: StatsStore) {
        val s = store.load()
        synchronized(lock) {
            bleFramesIn = s.bleFramesIn
            bleFramesOut = s.bleFramesOut
            bleReconnects = s.bleReconnects
            textMessagesIn = s.textMessagesIn
            textMessagesOut = s.textMessagesOut
            blobChunksIn = s.blobChunksIn
            blobChunksOut = s.blobChunksOut
            blobCrcRejects = s.blobCrcRejects
            blobPartialGcDrops = s.blobPartialGcDrops
            protoIn = s.protoIn
            protoOut = s.protoOut
            protoUnknownSchema = s.protoUnknownSchema
            echoProbesSent = s.echoProbesSent
            _state.value = snapshotLocked()
        }
    }

    // ── Internals ────────────────────────────────────────────────────────────

    private inline fun bump(mutate: () -> Unit) {
        synchronized(lock) {
            mutate()
            _state.value = snapshotLocked()
        }
    }

    private fun snapshotLocked(): Statistics {
        val (p50, p95) = rttPercentilesLocked()
        return Statistics(
            bleFramesIn = bleFramesIn,
            bleFramesOut = bleFramesOut,
            bleReconnects = bleReconnects,
            textMessagesIn = textMessagesIn,
            textMessagesOut = textMessagesOut,
            blobChunksIn = blobChunksIn,
            blobChunksOut = blobChunksOut,
            blobCrcRejects = blobCrcRejects,
            blobPartialGcDrops = blobPartialGcDrops,
            protoIn = protoIn,
            protoOut = protoOut,
            protoUnknownSchema = protoUnknownSchema,
            echoProbesSent = echoProbesSent,
            echoRttMsP50 = p50,
            echoRttMsP95 = p95,
        )
    }

    private fun rttPercentilesLocked(): Pair<Long?, Long?> {
        if (rttSize == 0) return null to null
        val sorted = LongArray(rttSize) { rttSamples[it] }.also { it.sort() }
        return sorted.percentile(0.50) to sorted.percentile(0.95)
    }

    private fun LongArray.percentile(q: Double): Long {
        // Nearest-rank: ceil(q * N) - 1, clamped to [0, N-1].
        val idx = (Math.ceil(q * size) - 1).toInt().coerceIn(0, size - 1)
        return this[idx]
    }

    companion object {
        const val DEFAULT_RTT_CAP = 256
    }
}
