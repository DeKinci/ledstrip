package io.tunnelchat

import java.io.File

/**
 * Library configuration. All values are captured at construction time; mutating the
 * instance after [Tunnelchat] construction has no effect.
 */
data class TunnelchatConfig(
    /** Directory holding the Room DB + rolling log files. Library owns the contents. */
    val archivePath: File,
    val logBufferBytes: Long = 2L * 1024 * 1024,
    /** Enables verbose payload logging + the `echoProbe` surface. Off by default. */
    val debugMode: Boolean = false,
    val echoIntervalMs: Long = 60_000,
    /** Default blob size cap. ~26 chunks of 79 bytes each, ~8s mesh airtime. */
    val maxBlobBytes: Int = 2048,
    /** Caller may raise [maxBlobBytes] up to this ceiling. */
    val maxBlobBytesHardCeiling: Int = 4096,
    /** Total outstanding bytes across parallel blob sends. Over-cap sends enter `Queued`. */
    val maxInFlightBlobBytes: Int = 8 * 1024,
    /** Receiver drops partial reassembly state after this much idle time. */
    val partialBlobGcMs: Long = 10L * 60_000,
) {
    init {
        require(maxBlobBytes in 1..maxBlobBytesHardCeiling) {
            "maxBlobBytes=$maxBlobBytes out of range 1..$maxBlobBytesHardCeiling"
        }
        require(maxInFlightBlobBytes >= maxBlobBytes) {
            "maxInFlightBlobBytes ($maxInFlightBlobBytes) must be >= maxBlobBytes ($maxBlobBytes)"
        }
    }
}
