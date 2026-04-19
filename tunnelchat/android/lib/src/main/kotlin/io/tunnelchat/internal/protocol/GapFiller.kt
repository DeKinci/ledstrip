package io.tunnelchat.internal.protocol

import io.tunnelchat.api.SenderId
import io.tunnelchat.api.Seq
import io.tunnelchat.internal.archive.MessageStore

/**
 * On reconnect: fetch server state, diff each peer's `highSeq` against the local
 * [MessageStore], and issue `GetMessages(sender, localHigh + 1)` for any gaps.
 *
 * Replies arrive asynchronously on [CommandDispatcher.messageReplies] and are routed
 * by `SessionCoordinator` through the same inbound pipeline as live `IncomingMsg`
 * pushes. `MessageStore.insert` is idempotent on (sender_id, seq), so overlapping
 * windows or duplicated replies do not double-insert.
 *
 * Gap-fill is not transactional — if the link drops mid-run, some `GetMessages`
 * commands may not complete. The next reconnect re-runs this and fills the rest.
 */
internal class GapFiller(
    private val dispatcher: CommandDispatcher,
    private val messageStore: MessageStore,
) {
    /**
     * Run one gap-fill pass. Returns the set of `(senderId, fromSeq)` requests that
     * were issued (useful for diagnostics/tests). Returns `failure` only if the
     * initial `GetState` call failed; individual `GetMessages` failures are swallowed
     * so one bad sender doesn't block the rest.
     */
    suspend fun fillGaps(): Result<List<Request>> {
        val state = dispatcher.getState().getOrElse { return Result.failure(it) }
        val issued = ArrayList<Request>()
        for (entry in state.entries) {
            val localHigh = messageStore.highSeq(entry.senderId)
            val from = if (localHigh == null) Seq(0u) else Seq((localHigh.raw + 1u).toUShort())
            if (needsFill(from, entry.highSeq)) {
                dispatcher.getMessages(entry.senderId, from)
                issued += Request(entry.senderId, from)
            }
        }
        return Result.success(issued)
    }

    data class Request(val senderId: SenderId, val fromSeq: Seq)

    /** True when there's at least one seq missing between `from` (inclusive) and `remoteHigh` (inclusive). */
    private fun needsFill(from: Seq, remoteHigh: Seq): Boolean = from.raw <= remoteHigh.raw
}
