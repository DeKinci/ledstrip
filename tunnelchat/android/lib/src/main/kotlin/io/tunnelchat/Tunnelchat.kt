package io.tunnelchat

import android.bluetooth.BluetoothDevice
import android.content.Context
import com.google.protobuf.MessageLite
import io.tunnelchat.api.BlobArrival
import io.tunnelchat.api.BlobHandle
import io.tunnelchat.api.BlobId
import io.tunnelchat.api.BlobReceiveProgress
import io.tunnelchat.api.ConnectionState
import io.tunnelchat.api.DiagnosticLog
import io.tunnelchat.api.EchoResult
import io.tunnelchat.api.MessageEnvelope
import io.tunnelchat.api.PeerInfo
import io.tunnelchat.api.ProtoArrival
import io.tunnelchat.api.ProtoRegistry
import io.tunnelchat.api.SelfInfo
import io.tunnelchat.api.SenderId
import io.tunnelchat.api.Seq
import io.tunnelchat.api.Statistics
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import java.io.Closeable

/**
 * Entry point into the tunnelchat library. One instance per process.
 *
 * Construction is cheap; no BLE I/O happens until [pair] / [connect]. [close] releases
 * all resources (BLE session, DB, coroutines). Flows returned by this class stop
 * emitting after [close].
 *
 * See `DESIGN.md` for architecture and invariants.
 *
 * NOTE: Phase 1 stub. All methods throw `NotImplementedError` until later phases wire
 * the internals. The public API surface is frozen — later phases fulfil, they do not
 * reshape.
 */
class Tunnelchat(
    @Suppress("UNUSED_PARAMETER") context: Context,
    @Suppress("UNUSED_PARAMETER") config: TunnelchatConfig,
) : Closeable {

    // ── Connection lifecycle ──────────────────────────────────────────────────
    val connectionState: StateFlow<ConnectionState>
        get() = TODO("Phase 10")

    suspend fun pair(device: BluetoothDevice): Result<Unit> = TODO("Phase 10")
    suspend fun connect(): Result<Unit> = TODO("Phase 10")
    suspend fun disconnect(): Unit = TODO("Phase 10")

    // ── Messaging ─────────────────────────────────────────────────────────────
    /** Send a plain text message. Max payload = 99 bytes (firmware Text cap). */
    suspend fun sendText(bytes: ByteArray): Result<MessageEnvelope> = TODO("Phase 10")
    suspend fun sendLocation(nodeA: UByte, nodeB: UByte): Result<MessageEnvelope> = TODO("Phase 10")

    val incomingMessages: SharedFlow<MessageEnvelope>
        get() = TODO("Phase 10")

    val peers: StateFlow<Map<SenderId, PeerInfo>>
        get() = TODO("Phase 10")

    /**
     * Cold snapshot of archived messages from [sender] starting at [fromSeq] (inclusive).
     * Emits the backlog and completes. For live updates, compose with
     * `incomingMessages.filter { it.senderId == sender }`.
     */
    fun messageHistory(sender: SenderId, fromSeq: Seq? = null): Flow<MessageEnvelope> =
        TODO("Phase 10")

    // ── Blob transport ────────────────────────────────────────────────────────
    suspend fun sendBlob(bytes: ByteArray): BlobHandle = TODO("Phase 10")

    val incomingBlobs: SharedFlow<BlobArrival>
        get() = TODO("Phase 10")

    val incomingBlobsInFlight: StateFlow<Map<BlobId, BlobReceiveProgress>>
        get() = TODO("Phase 10")

    // ── Proto transport ───────────────────────────────────────────────────────
    val protoRegistry: ProtoRegistry
        get() = TODO("Phase 10")

    suspend fun <T : MessageLite> sendProto(schemaId: UShort, message: T): BlobHandle =
        TODO("Phase 10")

    val incomingProtos: SharedFlow<ProtoArrival>
        get() = TODO("Phase 10")

    // ── Self-info + clock ─────────────────────────────────────────────────────
    suspend fun selfInfo(): Result<SelfInfo> = TODO("Phase 10")
    suspend fun setClock(unixSeconds: UInt): Result<Unit> = TODO("Phase 10")

    // ── Diagnostics ───────────────────────────────────────────────────────────
    val statistics: StateFlow<Statistics>
        get() = TODO("Phase 10")

    fun resetStatistics(): Unit = TODO("Phase 10")

    val diagnosticLog: DiagnosticLog
        get() = TODO("Phase 10")

    /** No-op (returns Timeout) unless `config.debugMode == true`. */
    suspend fun echoProbe(target: SenderId): Result<EchoResult> = TODO("Phase 10")

    override fun close() = TODO("Phase 10")
}
