package io.tunnelchat.internal.blob

import io.tunnelchat.api.BlobId
import io.tunnelchat.api.BlobSendProgress
import io.tunnelchat.api.TunnelchatError
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.withContext
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.filter
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlin.random.Random

/**
 * Fans outbound blobs into chunks and hands them to [SendChunk]. Enforces the
 * `maxInFlightBlobBytes` budget by queueing submissions past the cap.
 *
 * The sender does not know when (or whether) a remote peer reassembled the blob —
 * the mesh is broadcast + pull. `Transmitted` = "every chunk was accepted by the BLE
 * transport"; nothing stronger.
 */
internal class BlobSender(
    private val scope: CoroutineScope,
    private val maxBlobBytes: Int,
    private val maxInFlightBlobBytes: Int,
    private val sendChunk: SendChunk,
    private val newBlobId: () -> BlobId = { BlobId(Random.Default.nextLong().toULong()) },
) {
    /** Hands a single envelope-framed chunk to the BLE layer. Returns `false` if the
     *  link is down / write failed. */
    fun interface SendChunk {
        suspend fun send(payload: ByteArray): Boolean
    }

    private val budgetMutex = Mutex()
    private var inFlightBytes: Int = 0
    private val waiters = ArrayDeque<Waiter>()

    internal class Waiter(val bytes: Int, val signal: CompletableDeferred<Unit>)

    /**
     * Register a new outbound blob. Returns immediately with a handle whose progress
     * starts at [BlobSendProgress.Queued]; transitions to `InFlight` once the byte
     * budget admits it. Caller may `await` or observe progress on the handle.
     *
     * Throws [IllegalArgumentException] when `bytes` is empty (programmer bug).
     */
    fun enqueue(bytes: ByteArray): BlobHandleImpl {
        require(bytes.isNotEmpty()) { "blob bytes must not be empty" }
        val totalChunks = (bytes.size + BlobEnvelope.MAX_CHUNK_BYTES - 1) / BlobEnvelope.MAX_CHUNK_BYTES
        val handle = BlobHandleImpl(newBlobId(), totalChunks)
        if (bytes.size > maxBlobBytes) {
            handle.fail(TunnelchatError.PayloadTooLarge(maxBlobBytes))
            return handle
        }
        handle.attachJob(scope.launch {
            try {
                runSend(handle, bytes, totalChunks)
            } catch (ce: CancellationException) {
                handle.fail(TunnelchatError.Internal(ce))
                throw ce
            } catch (t: Throwable) {
                handle.fail(TunnelchatError.Internal(t))
            }
        })
        return handle
    }

    private suspend fun runSend(handle: BlobHandleImpl, bytes: ByteArray, totalChunks: Int) {
        var acquired = false
        try {
            acquire(bytes.size, handle)
            acquired = true
            handle.setInFlight(0)
            var sent = 0
            var offset = 0
            while (sent < totalChunks) {
                val end = minOf(offset + BlobEnvelope.MAX_CHUNK_BYTES, bytes.size)
                val chunk = bytes.copyOfRange(offset, end)
                val frame = BlobEnvelope.encode(handle.blobId, sent, totalChunks, chunk)
                val ok = sendChunk.send(frame)
                if (!ok) {
                    handle.fail(TunnelchatError.BleDisconnected)
                    return
                }
                sent += 1
                offset = end
                handle.setInFlight(sent)
            }
            handle.complete()
        } finally {
            // Cleanup must run even after cancellation — otherwise the budget leaks or
            // a stale waiter sits in the queue forever.
            withContext(NonCancellable) {
                if (acquired) release(bytes.size)
                else removeWaiter(handle)
            }
        }
    }

    private suspend fun acquire(bytes: Int, handle: BlobHandleImpl) {
        val signal = budgetMutex.withLock {
            if (inFlightBytes + bytes <= maxInFlightBlobBytes && waiters.isEmpty()) {
                inFlightBytes += bytes
                return
            }
            val d = CompletableDeferred<Unit>()
            val w = Waiter(bytes, d)
            waiters.addLast(w)
            handle.attachWaiter(w)
            d
        }
        signal.await()
    }

    private suspend fun release(bytes: Int) = budgetMutex.withLock {
        inFlightBytes -= bytes
        // FIFO wake — only wake the head to avoid out-of-order starvation.
        while (waiters.isNotEmpty() && inFlightBytes + waiters.first().bytes <= maxInFlightBlobBytes) {
            val w = waiters.removeFirst()
            inFlightBytes += w.bytes
            w.signal.complete(Unit)
        }
    }

    private suspend fun removeWaiter(handle: BlobHandleImpl) {
        val w = handle.waiter ?: return
        budgetMutex.withLock {
            waiters.remove(w)
            w.signal.cancel()
        }
    }

    internal inner class BlobHandleImpl(
        override val blobId: BlobId,
        val totalChunks: Int,
    ) : io.tunnelchat.api.BlobHandle() {
        private val _progress = MutableStateFlow<BlobSendProgress>(BlobSendProgress.Queued(totalChunks))
        override val progress: StateFlow<BlobSendProgress> = _progress.asStateFlow()

        @Volatile private var job: Job? = null
        @Volatile internal var waiter: Waiter? = null

        fun attachJob(j: Job) { job = j }
        fun attachWaiter(w: Waiter) { waiter = w }

        fun setInFlight(sent: Int) {
            waiter = null
            _progress.value = BlobSendProgress.InFlight(totalChunks, sent)
        }

        fun complete() {
            _progress.compareAndSet(_progress.value, BlobSendProgress.Transmitted(totalChunks))
        }

        fun fail(e: TunnelchatError) {
            val cur = _progress.value
            if (cur is BlobSendProgress.Transmitted || cur is BlobSendProgress.Failed) return
            _progress.value = BlobSendProgress.Failed(e)
        }

        override suspend fun awaitTransmitted(): Result<Unit> {
            val terminal = progress.filter {
                it is BlobSendProgress.Transmitted || it is BlobSendProgress.Failed
            }.first()
            return when (terminal) {
                is BlobSendProgress.Transmitted -> Result.success(Unit)
                is BlobSendProgress.Failed -> Result.failure(terminal.error)
                else -> error("unreachable")
            }
        }

        override fun cancel() {
            fail(TunnelchatError.Internal(CancellationException("cancelled by caller")))
            job?.cancel()
        }
    }
}
