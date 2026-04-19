package io.tunnelchat.internal.archive

import io.tunnelchat.api.Statistics

internal class StatsStore(private val dao: StatsDao) {
    /** Persist a snapshot. Single-row table, fully overwritten. */
    suspend fun flush(s: Statistics) {
        dao.put(
            StatsRow(
                bleFramesIn = s.bleFramesIn,
                bleFramesOut = s.bleFramesOut,
                bleReconnects = s.bleReconnects,
                textMessagesIn = s.textMessagesIn,
                textMessagesOut = s.textMessagesOut,
                blobChunksIn = s.blobChunksIn,
                blobChunksOut = s.blobChunksOut,
                blobCrcRejects = s.blobCrcRejects,
                blobPartialGcDrops = s.blobPartialGcDrops,
                protoIn = s.protoIn,
                protoOut = s.protoOut,
                protoUnknownSchema = s.protoUnknownSchema,
                echoProbesSent = s.echoProbesSent,
            )
        )
    }

    /** Load persisted aggregate. Returns zeroed [Statistics] if nothing stored yet. */
    suspend fun load(): Statistics =
        dao.get()?.let {
            Statistics(
                bleFramesIn = it.bleFramesIn,
                bleFramesOut = it.bleFramesOut,
                bleReconnects = it.bleReconnects,
                textMessagesIn = it.textMessagesIn,
                textMessagesOut = it.textMessagesOut,
                blobChunksIn = it.blobChunksIn,
                blobChunksOut = it.blobChunksOut,
                blobCrcRejects = it.blobCrcRejects,
                blobPartialGcDrops = it.blobPartialGcDrops,
                protoIn = it.protoIn,
                protoOut = it.protoOut,
                protoUnknownSchema = it.protoUnknownSchema,
                echoProbesSent = it.echoProbesSent,
            )
        } ?: Statistics()

    suspend fun clear() = dao.clear()
}
