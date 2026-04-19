package io.tunnelchat.api

sealed class TunnelchatError {
    data object PermissionMissing : TunnelchatError()
    data object BluetoothDisabled : TunnelchatError()
    data object NotPaired : TunnelchatError()
    data object Timeout : TunnelchatError()
    data class BleGatt(val statusCode: Int) : TunnelchatError()
    /** Outbound payload exceeds the firmware Text cap (99 bytes) or the proto/blob ceiling. */
    data class PayloadTooLarge(val limitBytes: Int) : TunnelchatError()
    data class UnknownSchema(val schemaId: UShort) : TunnelchatError()
    data class Internal(val cause: Throwable) : TunnelchatError()
}

/** Thrown only when the library is misused (programmer bugs). Recoverable failures
 *  surface as [TunnelchatError] inside `Result<T>` instead. */
class TunnelchatException(msg: String) : IllegalStateException(msg)
