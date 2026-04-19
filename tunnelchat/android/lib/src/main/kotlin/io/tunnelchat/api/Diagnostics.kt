package io.tunnelchat.api

import kotlinx.coroutines.flow.Flow

/** Immutable snapshot of library-internal counters + gauges. */
data class Statistics(
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
    val echoRttMsP50: Long? = null,
    val echoRttMsP95: Long? = null,
)

enum class LogLevel { Verbose, Debug, Info, Warn, Error }

data class LogEntry(
    val tsMs: Long,
    val level: LogLevel,
    val tag: String,
    val message: String,
)

interface DiagnosticLog {
    /** Cold snapshot of the rolling buffer, newest-last. */
    fun snapshot(): List<LogEntry>
    /** Hot stream of log entries as they are written. */
    val entries: Flow<LogEntry>
    fun setMinLevel(level: LogLevel)
    fun clear()
}
