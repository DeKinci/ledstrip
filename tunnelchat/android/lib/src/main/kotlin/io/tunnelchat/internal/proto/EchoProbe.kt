package io.tunnelchat.internal.proto

import io.tunnelchat.api.EchoResult
import io.tunnelchat.api.SenderId
import io.tunnelchat.api.TunnelchatError
import io.tunnelchat.builtin.Echo
import io.tunnelchat.builtin.Pong
import io.tunnelchat.internal.stats.StatsRecorder
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.withTimeout
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicInteger

/**
 * Issues an `Echo` via [ProtoSender], awaits a matching `Pong` from the specified
 * [SenderId], and reports RTT. Pong handling is driven by [ProtoReceiver] calling
 * [onPong] — out-of-target or unknown pingIds are dropped silently.
 */
internal class EchoProbe(
    private val sender: ProtoSender,
    private val stats: StatsRecorder? = null,
    private val nowMs: () -> Long = System::currentTimeMillis,
) {
    private data class Pending(
        val startMs: Long,
        val target: SenderId,
        val signal: CompletableDeferred<EchoResult>,
    )

    private val pending = ConcurrentHashMap<UInt, Pending>()

    // Positive-only 31-bit range so the proto field (uint32) is unambiguous.
    private val counter = AtomicInteger(0)

    suspend fun probe(target: SenderId, timeoutMs: Long): Result<EchoResult> {
        val pingId = (counter.incrementAndGet() and 0x7FFFFFFF).toUInt()
        val startMs = nowMs()
        val signal = CompletableDeferred<EchoResult>()
        pending[pingId] = Pending(startMs, target, signal)
        try {
            val echo = Echo.newBuilder()
                .setPingId(pingId.toInt())
                .setOriginTs((startMs / 1000L).toInt())
                .build()
            sender.send(BuiltinSchemas.ECHO.toUShort(), echo)
            stats?.incEchoProbesSent()
            return try {
                val result = withTimeout(timeoutMs) { signal.await() }
                stats?.recordEchoRtt(result.rttMs)
                Result.success(result)
            } catch (_: TimeoutCancellationException) {
                Result.failure(TunnelchatError.Timeout)
            }
        } finally {
            pending.remove(pingId)
        }
    }

    /** Called by [ProtoReceiver] on Pong arrival. */
    fun onPong(senderId: SenderId, pong: Pong) {
        val pingId = pong.pingId.toUInt()
        val p = pending[pingId] ?: return
        if (p.target != senderId) return
        p.signal.complete(EchoResult(rttMs = nowMs() - p.startMs, pingId = pingId))
    }
}
