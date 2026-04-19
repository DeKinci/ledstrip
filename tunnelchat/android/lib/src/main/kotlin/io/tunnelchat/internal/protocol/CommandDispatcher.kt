package io.tunnelchat.internal.protocol

import io.tunnelchat.api.SenderId
import io.tunnelchat.api.Seq
import io.tunnelchat.api.TunnelchatError
import io.tunnelchat.internal.wire.CommandFrame
import io.tunnelchat.internal.wire.Notification
import io.tunnelchat.internal.wire.NotificationFrame
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeout

/**
 * Serialises outbound BLE commands and correlates request→response where the firmware
 * provides a response (`GetState`→`StateResp`, `GetSelfInfo`→`SelfInfoResp`). Fire-and-
 * forget commands (`SetClock`, `SetLocation`, `SendText`) return immediately on send.
 *
 * `GetMessages` has no terminator frame on the wire — replies arrive as one or more
 * `MessageResp` (0x81) frames and are surfaced on [messageReplies] for Phase 9's
 * GapFiller to consume. Pushes (`IncomingMsg` 0x82, `PresenceEvent` 0x83) fan out on
 * [incomingMessages] / [presenceEvents].
 *
 * Concurrency: a single [Mutex] serialises the entire request/response cycle for
 * correlated commands, so at most one `GetState`/`GetSelfInfo` is outstanding at a time.
 * Fire-and-forget commands also take the mutex briefly to preserve wire order.
 *
 * Lifecycle: call [start] once with the session scope; [close] cancels the inbound
 * pump and fails any in-flight awaiter.
 */
internal class CommandDispatcher(
    private val transport: CommandTransport,
    private val defaultTimeoutMs: Long = DEFAULT_TIMEOUT_MS,
) {
    private val sendMutex = Mutex()

    // Accessed only while holding [sendMutex] (command path) or on the inbound
    // collector (completion path). Each slot is at most one outstanding request.
    @Volatile private var pendingState: CompletableDeferred<Notification.StateResp>? = null
    @Volatile private var pendingSelfInfo: CompletableDeferred<Notification.SelfInfoResp>? = null

    private val _incomingMessages = MutableSharedFlow<Notification.MessageFrame>(
        extraBufferCapacity = 64,
        onBufferOverflow = BufferOverflow.DROP_OLDEST,
    )
    val incomingMessages: SharedFlow<Notification.MessageFrame> = _incomingMessages.asSharedFlow()

    private val _messageReplies = MutableSharedFlow<Notification.MessageFrame>(
        extraBufferCapacity = 256,
        onBufferOverflow = BufferOverflow.DROP_OLDEST,
    )
    val messageReplies: SharedFlow<Notification.MessageFrame> = _messageReplies.asSharedFlow()

    private val _presenceEvents = MutableSharedFlow<Notification.PresenceFrame>(
        extraBufferCapacity = 64,
        onBufferOverflow = BufferOverflow.DROP_OLDEST,
    )
    val presenceEvents: SharedFlow<Notification.PresenceFrame> = _presenceEvents.asSharedFlow()

    private val _malformed = MutableSharedFlow<Notification.Malformed>(
        extraBufferCapacity = 32,
        onBufferOverflow = BufferOverflow.DROP_OLDEST,
    )
    /** Emitted for every malformed frame decoded. Primarily for diagnostics/tests. */
    val malformed: SharedFlow<Notification.Malformed> = _malformed.asSharedFlow()

    private var inboundJob: Job? = null

    fun start(scope: CoroutineScope) {
        if (inboundJob?.isActive == true) return
        inboundJob = scope.launch {
            transport.inbound.collect(::onFrame)
        }
    }

    fun close() {
        inboundJob?.cancel()
        inboundJob = null
        pendingState?.cancel()
        pendingState = null
        pendingSelfInfo?.cancel()
        pendingSelfInfo = null
    }

    // ── Fire-and-forget commands ─────────────────────────────────────────────

    suspend fun setClock(unixSeconds: UInt): Result<Unit> =
        sendFrame(CommandFrame.setClock(unixSeconds))

    suspend fun setLocation(nodeA: UByte, nodeB: UByte): Result<Unit> =
        sendFrame(CommandFrame.setLocation(nodeA, nodeB))

    suspend fun sendText(bytes: ByteArray): Result<Unit> {
        val frame = CommandFrame.sendText(bytes)
            ?: return Result.failure(TunnelchatError.PayloadTooLarge(CommandFrame.MAX_TEXT_BYTES))
        return sendFrame(frame)
    }

    suspend fun getMessages(sender: SenderId, fromSeq: Seq): Result<Unit> =
        sendFrame(CommandFrame.getMessages(sender, fromSeq))

    // ── Correlated commands ──────────────────────────────────────────────────

    suspend fun getState(timeoutMs: Long = defaultTimeoutMs): Result<Notification.StateResp> =
        sendMutex.withLock {
            val d = CompletableDeferred<Notification.StateResp>()
            pendingState = d
            try {
                if (!transport.send(CommandFrame.getState())) {
                    return@withLock Result.failure(TunnelchatError.BleDisconnected)
                }
                awaitWithTimeout(d, timeoutMs)
            } finally {
                if (pendingState === d) pendingState = null
            }
        }

    suspend fun getSelfInfo(timeoutMs: Long = defaultTimeoutMs): Result<Notification.SelfInfoResp> =
        sendMutex.withLock {
            val d = CompletableDeferred<Notification.SelfInfoResp>()
            pendingSelfInfo = d
            try {
                if (!transport.send(CommandFrame.getSelfInfo())) {
                    return@withLock Result.failure(TunnelchatError.BleDisconnected)
                }
                awaitWithTimeout(d, timeoutMs)
            } finally {
                if (pendingSelfInfo === d) pendingSelfInfo = null
            }
        }

    // ── Internals ────────────────────────────────────────────────────────────

    private suspend fun sendFrame(frame: ByteArray): Result<Unit> = sendMutex.withLock {
        if (transport.send(frame)) Result.success(Unit)
        else Result.failure(TunnelchatError.BleDisconnected)
    }

    private suspend fun <T> awaitWithTimeout(d: CompletableDeferred<T>, timeoutMs: Long): Result<T> =
        try {
            Result.success(withTimeout(timeoutMs) { d.await() })
        } catch (_: TimeoutCancellationException) {
            Result.failure(TunnelchatError.Timeout)
        }

    private fun onFrame(frame: ByteArray) {
        when (val n = NotificationFrame.decode(frame)) {
            is Notification.StateResp -> {
                pendingState?.complete(n)
            }
            is Notification.SelfInfoResp -> {
                pendingSelfInfo?.complete(n)
            }
            is Notification.MessageFrame -> {
                if (n.push) _incomingMessages.tryEmit(n) else _messageReplies.tryEmit(n)
            }
            is Notification.PresenceFrame -> {
                _presenceEvents.tryEmit(n)
            }
            is Notification.Malformed -> {
                _malformed.tryEmit(n)
            }
        }
    }

    companion object {
        const val DEFAULT_TIMEOUT_MS: Long = 5_000L
    }
}
