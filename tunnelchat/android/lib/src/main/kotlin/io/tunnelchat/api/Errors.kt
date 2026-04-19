package io.tunnelchat.api

/**
 * Recoverable failures surfaced inside `Result<T>.failure(error)`. Extends [Throwable]
 * so it can be the failure payload directly; callers do
 * `result.exceptionOrNull() as? TunnelchatError` to match.
 */
sealed class TunnelchatError(cause: Throwable? = null) : Throwable(cause) {
    data object PermissionMissing : TunnelchatError()
    data object BluetoothDisabled : TunnelchatError()
    data object NotPaired : TunnelchatError()
    data object Timeout : TunnelchatError()
    data class BleGatt(val statusCode: Int) : TunnelchatError()
    /** Reconnect attempts exhausted; the link is down. */
    data object BleDisconnected : TunnelchatError()
    /** Outbound payload exceeds the firmware Text cap (99 bytes) or the proto/blob ceiling. */
    data class PayloadTooLarge(val limitBytes: Int) : TunnelchatError()
    data class UnknownSchema(val schemaId: UShort) : TunnelchatError()
    data class Internal(val underlying: Throwable) : TunnelchatError(underlying)
}

/** Thrown only when the library is misused (programmer bugs). Recoverable failures
 *  surface as [TunnelchatError] inside `Result<T>` instead. */
class TunnelchatException(msg: String) : IllegalStateException(msg)
