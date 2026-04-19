package io.tunnelchat.internal.protocol

import io.tunnelchat.api.BlobArrival
import io.tunnelchat.api.DeliveryState
import io.tunnelchat.api.Message
import io.tunnelchat.api.MessageEnvelope
import io.tunnelchat.api.PeerInfo
import io.tunnelchat.api.Presence
import io.tunnelchat.api.SenderId
import io.tunnelchat.api.Seq
import io.tunnelchat.internal.archive.MessageStore
import io.tunnelchat.internal.archive.PresenceStore
import io.tunnelchat.internal.blob.BlobReceiver
import io.tunnelchat.internal.proto.ProtoReceiver
import io.tunnelchat.internal.stats.StatsRecorder
import io.tunnelchat.internal.wire.Notification
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

/**
 * Glues together [CommandDispatcher], [BlobReceiver], [ProtoReceiver], and the archive
 * stores. Exposes the three public reactive streams surfaced by the `Tunnelchat`
 * facade: [incomingMessages], [incomingBlobs], [peers].
 *
 * Inbound routing:
 *   dispatcher.incomingMessages (push 0x82)      ─┐
 *   dispatcher.messageReplies   (reply 0x81)     ─┴─► try blob.accept
 *                                                    ├─ absorbed → chunk counted, no emission
 *                                                    └─ not a chunk → MessageStore.insert (idempotent)
 *                                                                      → emit on incomingMessages
 *   dispatcher.presenceEvents                      ─► PresenceStore.upsert + peers update
 *   blobReceiver.arrivals                          ─► try protoReceiver.accept
 *                                                    ├─ absorbed → no emission (built-in or app proto handled)
 *                                                    └─ raw blob → emit on incomingBlobs
 *
 * On connect ([onConnected]): issues `setClock` (best-effort) and kicks off [GapFiller]
 * exactly once.
 */
internal class SessionCoordinator(
    private val dispatcher: CommandDispatcher,
    private val blobReceiver: BlobReceiver,
    private val protoReceiver: ProtoReceiver,
    private val messageStore: MessageStore,
    private val presenceStore: PresenceStore,
    private val gapFiller: GapFiller,
    private val stats: StatsRecorder? = null,
    private val nowMs: () -> Long = System::currentTimeMillis,
) {
    private val _incomingMessages = MutableSharedFlow<MessageEnvelope>(
        extraBufferCapacity = 64,
        onBufferOverflow = BufferOverflow.DROP_OLDEST,
    )
    val incomingMessages: SharedFlow<MessageEnvelope> = _incomingMessages.asSharedFlow()

    private val _incomingBlobs = MutableSharedFlow<BlobArrival>(
        extraBufferCapacity = 32,
        onBufferOverflow = BufferOverflow.DROP_OLDEST,
    )
    val incomingBlobs: SharedFlow<BlobArrival> = _incomingBlobs.asSharedFlow()

    private val _peers = MutableStateFlow<Map<SenderId, PeerInfo>>(emptyMap())
    val peers: StateFlow<Map<SenderId, PeerInfo>> = _peers.asStateFlow()

    private val peerMutex = Mutex()
    private val jobs = mutableListOf<Job>()

    /** Wire up the collectors. Idempotent — calling twice returns without duplicating. */
    fun start(scope: CoroutineScope) {
        if (jobs.isNotEmpty()) return
        jobs += scope.launch {
            dispatcher.incomingMessages.collect { onMessageFrame(it) }
        }
        jobs += scope.launch {
            dispatcher.messageReplies.collect { onMessageFrame(it) }
        }
        jobs += scope.launch {
            dispatcher.presenceEvents.collect { onPresence(it) }
        }
        jobs += scope.launch {
            blobReceiver.arrivals.collect { onBlobArrival(it) }
        }
    }

    fun stop() {
        jobs.forEach { it.cancel() }
        jobs.clear()
    }

    /**
     * Call exactly once per BLE session, after `connect()` succeeds. Best-effort
     * clock sync followed by a gap-fill pass. The caller's `connect()` result does
     * not depend on either sub-call succeeding.
     */
    suspend fun onConnected(unixSeconds: UInt): Result<List<GapFiller.Request>> {
        dispatcher.setClock(unixSeconds)
        return gapFiller.fillGaps()
    }

    // ── Internals ────────────────────────────────────────────────────────────

    private suspend fun onMessageFrame(frame: Notification.MessageFrame) {
        // Text payloads may be blob chunks; give the blob receiver first claim.
        val text = frame.message as? Message.Text
        if (text != null && blobReceiver.accept(frame.senderId, text.bytes)) {
            stats?.incBlobChunksIn()
            return
        }
        val env = MessageEnvelope(
            senderId = frame.senderId,
            seq = frame.seq,
            timestamp = frame.timestamp,
            receivedAtMs = nowMs(),
            message = frame.message,
            // Delivery state is sender-side; for inbound messages we record `Pending`
            // as an inert default (the enum has no dedicated "received" slot and apps
            // are expected to key off envelope.senderId vs. self).
            delivery = DeliveryState.Pending,
        )
        val isNew = messageStore.insert(env)
        if (!isNew) return
        stats?.incTextMessagesIn()
        _incomingMessages.tryEmit(env)
        updatePeerOnMessage(env)
    }

    private suspend fun updatePeerOnMessage(env: MessageEnvelope) = peerMutex.withLock {
        val cur = _peers.value
        val prior = cur[env.senderId]
        val nextHigh = if (prior == null || env.seq.raw > prior.highSeq.raw) env.seq else prior.highSeq
        val nextLoc = when (val m = env.message) {
            is Message.Location -> m
            else -> prior?.lastLocation
        }
        val updated = PeerInfo(
            senderId = env.senderId,
            presence = prior?.presence ?: Presence.Online,
            lastHeardMs = env.receivedAtMs,
            highSeq = nextHigh,
            lastLocation = nextLoc,
        )
        _peers.value = cur + (env.senderId to updated)
    }

    private suspend fun onPresence(p: Notification.PresenceFrame) = peerMutex.withLock {
        val now = nowMs()
        presenceStore.upsert(p.senderId, p.presence, now)
        val cur = _peers.value
        val prior = cur[p.senderId]
        val updated = PeerInfo(
            senderId = p.senderId,
            presence = p.presence,
            lastHeardMs = now,
            highSeq = prior?.highSeq ?: Seq(0u),
            lastLocation = prior?.lastLocation,
        )
        _peers.value = cur + (p.senderId to updated)
    }

    private suspend fun onBlobArrival(arrival: BlobArrival) {
        if (protoReceiver.accept(arrival.senderId, arrival.bytes)) return
        _incomingBlobs.tryEmit(arrival)
    }
}
