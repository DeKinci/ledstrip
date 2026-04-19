package io.tunnelchat

import android.bluetooth.BluetoothDevice
import android.content.Context
import com.google.protobuf.MessageLite
import io.tunnelchat.api.BlobArrival
import io.tunnelchat.api.BlobHandle
import io.tunnelchat.api.BlobId
import io.tunnelchat.api.BlobReceiveProgress
import io.tunnelchat.api.ConnectionState
import io.tunnelchat.api.DeliveryState
import io.tunnelchat.api.DiagnosticLog
import io.tunnelchat.api.EchoResult
import io.tunnelchat.api.LogEntry
import io.tunnelchat.api.Message
import io.tunnelchat.api.MessageEnvelope
import io.tunnelchat.api.PeerInfo
import io.tunnelchat.api.ProtoArrival
import io.tunnelchat.api.ProtoRegistry
import io.tunnelchat.api.SelfInfo
import io.tunnelchat.api.SenderId
import io.tunnelchat.api.Seq
import io.tunnelchat.api.Statistics
import io.tunnelchat.api.TunnelchatError
import io.tunnelchat.builtin.Pong
import io.tunnelchat.internal.archive.LogStore
import io.tunnelchat.internal.archive.MessageStore
import io.tunnelchat.internal.archive.PresenceStore
import io.tunnelchat.internal.archive.StatsStore
import io.tunnelchat.internal.archive.TunnelchatDatabase
import io.tunnelchat.internal.ble.BleSession
import io.tunnelchat.internal.blob.BlobReceiver
import io.tunnelchat.internal.blob.BlobSender
import io.tunnelchat.internal.log.DiagLogImpl
import io.tunnelchat.internal.proto.BuiltinSchemas
import io.tunnelchat.internal.proto.EchoProbe
import io.tunnelchat.internal.proto.ProtoReceiver
import io.tunnelchat.internal.proto.ProtoRegistryImpl
import io.tunnelchat.internal.proto.ProtoSender
import io.tunnelchat.internal.protocol.CommandDispatcher
import io.tunnelchat.internal.protocol.CommandTransport
import io.tunnelchat.internal.protocol.GapFiller
import io.tunnelchat.internal.protocol.SessionCoordinator
import io.tunnelchat.internal.stats.StatsRecorder
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.launch
import java.io.Closeable

/**
 * Entry point into the tunnelchat library. One instance per process.
 *
 * Construction is cheap; no BLE I/O happens until [pair] / [connect]. [close] releases
 * all resources (BLE session, DB, coroutines). Flows returned by this class stop
 * emitting after [close].
 */
class Tunnelchat internal constructor(
    private val appContext: Context,
    private val config: TunnelchatConfig,
    private val db: TunnelchatDatabase,
    private val transport: CommandTransport,
    /** Non-null when [transport] is also a real [BleSession]. */
    private val bleSession: BleSession?,
) : Closeable {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    // ── Archive ──────────────────────────────────────────────────────────────
    private val messageStore = MessageStore(db.messages())
    private val presenceStore = PresenceStore(db.presence())
    private val statsStore = StatsStore(db.stats())
    private val logStore = LogStore(db.logs())

    // ── Stats + log ──────────────────────────────────────────────────────────
    private val stats = StatsRecorder()
    private val log = DiagLogImpl(
        scope = scope,
        persistor = object : DiagLogImpl.Persistor {
            override suspend fun append(entry: LogEntry) { logStore.append(entry) }
            override suspend fun clear() { logStore.clear() }
        },
    )

    // ── Protocol layer ───────────────────────────────────────────────────────
    private val dispatcher = CommandDispatcher(transport)

    // ── Blob layer ───────────────────────────────────────────────────────────
    private val blobReceiver = BlobReceiver(partialBlobGcMs = config.partialBlobGcMs)
    private val blobSender = BlobSender(
        scope = scope,
        maxBlobBytes = config.maxBlobBytes,
        maxInFlightBlobBytes = config.maxInFlightBlobBytes,
        sendChunk = BlobSender.SendChunk { bytes ->
            val ok = dispatcher.sendText(bytes).isSuccess
            if (ok) stats.incBlobChunksOut()
            ok
        },
    )

    // ── Proto layer ──────────────────────────────────────────────────────────
    private val protoRegistryImpl = ProtoRegistryImpl()
    private val protoSender = ProtoSender(blobSender, stats)
    private val echoProbeImpl = EchoProbe(protoSender, stats)
    private val protoReceiver = ProtoReceiver(
        registry = protoRegistryImpl,
        onEcho = { _, echo ->
            // Auto-reply: echo the originator's pingId + timestamp verbatim.
            val pong = Pong.newBuilder()
                .setPingId(echo.pingId)
                .setOriginTs(echo.originTs)
                .build()
            protoSender.send(BuiltinSchemas.PONG.toUShort(), pong)
        },
        onPong = { senderId, pong -> echoProbeImpl.onPong(senderId, pong) },
        stats = stats,
    )

    // ── Orchestration ────────────────────────────────────────────────────────
    private val gapFiller = GapFiller(dispatcher, messageStore)
    private val coordinator = SessionCoordinator(
        dispatcher = dispatcher,
        blobReceiver = blobReceiver,
        protoReceiver = protoReceiver,
        messageStore = messageStore,
        presenceStore = presenceStore,
        gapFiller = gapFiller,
        stats = stats,
    )

    // ── Public reactive surface ──────────────────────────────────────────────
    private val _incomingProtos = MutableSharedFlow<ProtoArrival>(
        extraBufferCapacity = 32,
        onBufferOverflow = BufferOverflow.DROP_OLDEST,
    )

    private val _inFlightBlobs = MutableStateFlow<Map<BlobId, BlobReceiveProgress>>(emptyMap())

    @Volatile private var pairedDevice: BluetoothDevice? = null
    @Volatile private var selfDeviceId: SenderId? = null

    init {
        // Pump proto arrivals → public flow.
        scope.launch {
            protoReceiver.arrivals.collect { _incomingProtos.tryEmit(it) }
        }
        // Maintain in-flight blob receive map.
        scope.launch {
            blobReceiver.progress.collect { p ->
                _inFlightBlobs.value = if (p.receivedChunks >= p.totalChunks) {
                    _inFlightBlobs.value - p.blobId
                } else {
                    _inFlightBlobs.value + (p.blobId to p)
                }
            }
        }
        scope.launch {
            blobReceiver.arrivals.collect { a ->
                _inFlightBlobs.value = _inFlightBlobs.value - a.blobId
            }
        }
        // Seed stats from disk (best effort).
        scope.launch { runCatching { stats.loadFrom(statsStore) } }
    }

    // ── Connection lifecycle ──────────────────────────────────────────────────
    val connectionState: StateFlow<ConnectionState>
        get() = bleSession?.state ?: noBleStateFlow

    suspend fun pair(device: BluetoothDevice): Result<Unit> {
        pairedDevice = device
        return Result.success(Unit)
    }

    suspend fun connect(): Result<Unit> {
        bleSession?.let { ble ->
            val device = pairedDevice
                ?: return Result.failure(TunnelchatError.NotPaired)
            ble.connect(device)
            // Suspend until Connected or Error.
            val terminal = ble.state.first {
                it is ConnectionState.Connected || it is ConnectionState.Error
            }
            if (terminal is ConnectionState.Error) {
                return Result.failure(terminal.err)
            }
        }
        dispatcher.start(scope)
        coordinator.start(scope)
        // Best-effort, in-background: cache self deviceId, sync clock, gap-fill. None of
        // these block the connect() result — the link is up; failures here don't undo that.
        scope.launch {
            dispatcher.getSelfInfo().onSuccess { selfDeviceId = SenderId(it.deviceId) }
        }
        scope.launch {
            val nowSeconds = (System.currentTimeMillis() / 1000L).toUInt()
            coordinator.onConnected(nowSeconds)
        }
        return Result.success(Unit)
    }

    suspend fun disconnect() {
        bleSession?.disconnect()
        coordinator.stop()
        dispatcher.close()
    }

    // ── Messaging ─────────────────────────────────────────────────────────────
    /** Send a plain text message. Max payload = 99 bytes (firmware Text cap). */
    suspend fun sendText(bytes: ByteArray): Result<MessageEnvelope> {
        val r = dispatcher.sendText(bytes)
        if (r.isFailure) return Result.failure(r.exceptionOrNull()!!)
        stats.incTextMessagesOut()
        val now = System.currentTimeMillis()
        // Firmware does not echo the assigned seq back over BLE today, so the synthesized
        // envelope carries Seq(0u). When the proposed `mesh-visible` ack lands, this flips
        // to a true seq + DeliveryState.MeshVisible without an API change.
        return Result.success(
            MessageEnvelope(
                senderId = selfDeviceId ?: SenderId(0u),
                seq = Seq(0u),
                timestamp = (now / 1000L).toUInt(),
                receivedAtMs = now,
                message = Message.Text(bytes),
                delivery = DeliveryState.SentToAbonent,
            )
        )
    }

    suspend fun sendLocation(nodeA: UByte, nodeB: UByte): Result<MessageEnvelope> {
        val r = dispatcher.setLocation(nodeA, nodeB)
        if (r.isFailure) return Result.failure(r.exceptionOrNull()!!)
        val now = System.currentTimeMillis()
        return Result.success(
            MessageEnvelope(
                senderId = selfDeviceId ?: SenderId(0u),
                seq = Seq(0u),
                timestamp = (now / 1000L).toUInt(),
                receivedAtMs = now,
                message = Message.Location(nodeA, nodeB),
                delivery = DeliveryState.SentToAbonent,
            )
        )
    }

    val incomingMessages: SharedFlow<MessageEnvelope>
        get() = coordinator.incomingMessages

    val peers: StateFlow<Map<SenderId, PeerInfo>>
        get() = coordinator.peers

    /**
     * Cold snapshot of archived messages from [sender] starting at [fromSeq] (inclusive).
     * Emits the backlog and completes. For live updates, compose with
     * `incomingMessages.filter { it.senderId == sender }`.
     */
    fun messageHistory(sender: SenderId, fromSeq: Seq? = null): Flow<MessageEnvelope> = flow {
        val from = fromSeq ?: Seq(0u)
        val to = Seq(UShort.MAX_VALUE)
        for (env in messageStore.range(sender, from, to)) emit(env)
    }

    // ── Blob transport ────────────────────────────────────────────────────────
    suspend fun sendBlob(bytes: ByteArray): BlobHandle = blobSender.enqueue(bytes)

    val incomingBlobs: SharedFlow<BlobArrival>
        get() = coordinator.incomingBlobs

    val incomingBlobsInFlight: StateFlow<Map<BlobId, BlobReceiveProgress>>
        get() = _inFlightBlobs.asStateFlow()

    // ── Proto transport ───────────────────────────────────────────────────────
    val protoRegistry: ProtoRegistry
        get() = protoRegistryImpl

    suspend fun <T : MessageLite> sendProto(schemaId: UShort, message: T): BlobHandle {
        require(schemaId.toInt() >= ProtoRegistryImpl.RESERVED_MAX_EXCL) {
            "schemaId ${schemaId.toInt()} is reserved for library built-ins"
        }
        return protoSender.send(schemaId, message)
    }

    val incomingProtos: SharedFlow<ProtoArrival>
        get() = _incomingProtos.asSharedFlow()

    // ── Self-info + clock ─────────────────────────────────────────────────────
    suspend fun selfInfo(): Result<SelfInfo> =
        dispatcher.getSelfInfo().map {
            val si = SelfInfo(
                deviceId = it.deviceId,
                clockUnix = it.clockUnix,
                activeSenders = it.activeSenders,
                bootCount = it.bootCount,
            )
            selfDeviceId = SenderId(si.deviceId)
            si
        }

    suspend fun setClock(unixSeconds: UInt): Result<Unit> = dispatcher.setClock(unixSeconds)

    // ── Diagnostics ───────────────────────────────────────────────────────────
    val statistics: StateFlow<Statistics>
        get() = stats.state

    fun resetStatistics() = stats.reset()

    val diagnosticLog: DiagnosticLog
        get() = log

    /** No-op (returns Timeout) unless `config.debugMode == true`. */
    suspend fun echoProbe(target: SenderId): Result<EchoResult> {
        if (!config.debugMode) return Result.failure(TunnelchatError.Timeout)
        return echoProbeImpl.probe(target, timeoutMs = config.echoIntervalMs)
    }

    override fun close() {
        coordinator.stop()
        dispatcher.close()
        bleSession?.close()
        scope.coroutineContext[Job]?.cancel()
        runCatching { db.close() }
    }

    companion object {
        private val noBleStateFlow: StateFlow<ConnectionState> =
            MutableStateFlow(ConnectionState.Disconnected).asStateFlow()

        /** Public production constructor — wires a real [BleSession] and on-disk Room DB. */
        operator fun invoke(context: Context, config: TunnelchatConfig): Tunnelchat {
            val ctx = context.applicationContext
            val ble = BleSession(ctx)
            return Tunnelchat(
                appContext = ctx,
                config = config,
                db = TunnelchatDatabase.open(ctx),
                transport = ble,
                bleSession = ble,
            )
        }

        /** Test seam: in-memory DB + caller-supplied transport (no BLE). */
        internal fun forTest(
            context: Context,
            config: TunnelchatConfig,
            transport: CommandTransport,
        ): Tunnelchat = Tunnelchat(
            appContext = context.applicationContext,
            config = config,
            db = TunnelchatDatabase.inMemory(context.applicationContext),
            transport = transport,
            bleSession = null,
        )
    }
}
