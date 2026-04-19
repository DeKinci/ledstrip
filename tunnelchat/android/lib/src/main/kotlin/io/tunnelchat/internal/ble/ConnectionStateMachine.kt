package io.tunnelchat.internal.ble

import android.bluetooth.BluetoothDevice
import io.tunnelchat.api.ConnectionState
import io.tunnelchat.api.TunnelchatError

/**
 * Pure FSM for the BLE link.
 *
 * Inputs are [Event]s posted by the BLE bridge (or the facade, for `connect`/`disconnect`).
 * Outputs are the new [ConnectionState] plus any [Action]s the caller must perform.
 *
 * Reasoning is deliberately stateless beyond [state] + [attempt] so the machine can be
 * unit-tested on the JVM without Android types other than [BluetoothDevice], which is a
 * type-only reference (no methods called).
 *
 * Transitions:
 *
 * ```
 *   Disconnected ──Connect(d)──► Connecting(d) ──GattConnected──► (services/MTU…) ──Ready──► Connected(d, mtu)
 *                                       │                                                          │
 *                                       └─────────GattFailure──┐                                   │
 *                                                              ▼                                   │
 *   Reconnecting(n) ◄──BackoffElapsed──── (n < maxAttempts) ◄──┴────────GattDisconnected───────────┘
 *         │
 *         └──n ≥ maxAttempts──► Disconnected (Error emitted as side effect)
 *
 *   Disconnect from any non-Disconnected state → Disconnected.
 * ```
 *
 * `Scanning` is reserved in [ConnectionState] but not entered for v1 — pairing is
 * caller-driven (`pair(BluetoothDevice)`).
 */
internal class ConnectionStateMachine(
    private val maxReconnectAttempts: Int = DEFAULT_MAX_RECONNECT,
    private val backoffMs: (attempt: Int) -> Long = ::defaultBackoff,
) {
    sealed class Event {
        data class Connect(val device: BluetoothDevice) : Event()
        data object Disconnect : Event()
        data object GattConnected : Event()
        data class GattDisconnected(val statusCode: Int) : Event()
        data object ServicesDiscovered : Event()
        data class MtuChanged(val mtu: Int) : Event()
        data class Failure(val err: TunnelchatError) : Event()
        data object BackoffElapsed : Event()
    }

    sealed class Action {
        data class Connect(val device: BluetoothDevice) : Action()
        data object Disconnect : Action()
        data object DiscoverServices : Action()
        data object RequestMtu : Action()
        data object EnableNotifications : Action()
        data class ScheduleReconnect(val delayMs: Long, val attempt: Int) : Action()
    }

    data class Transition(val state: ConnectionState, val actions: List<Action>)

    private var _state: ConnectionState = ConnectionState.Disconnected
    val state: ConnectionState get() = _state

    /** Last-known device so we can re-issue [Action.Connect] on backoff. */
    private var lastDevice: BluetoothDevice? = null
    /** Reconnect attempt count, 1-based. */
    private var attempt: Int = 0

    fun process(event: Event): Transition {
        val (next, actions) = when (val s = _state) {
            ConnectionState.Disconnected -> handleDisconnected(event)
            ConnectionState.Scanning -> handleStray(event)
            is ConnectionState.Connecting -> handleConnecting(s, event)
            is ConnectionState.Connected -> handleConnected(s, event)
            is ConnectionState.Reconnecting -> handleReconnecting(s, event)
            is ConnectionState.Error -> handleStray(event)
        }
        _state = next
        return Transition(next, actions)
    }

    private fun handleDisconnected(e: Event): Pair<ConnectionState, List<Action>> = when (e) {
        is Event.Connect -> {
            lastDevice = e.device
            attempt = 0
            ConnectionState.Connecting(e.device) to listOf(Action.Connect(e.device))
        }
        else -> noop()
    }

    private fun handleConnecting(
        s: ConnectionState.Connecting,
        e: Event,
    ): Pair<ConnectionState, List<Action>> = when (e) {
        Event.GattConnected -> s to listOf(Action.RequestMtu, Action.DiscoverServices)
        Event.ServicesDiscovered -> s to listOf(Action.EnableNotifications)
        is Event.MtuChanged -> ConnectionState.Connected(s.device, e.mtu) to emptyList()
        is Event.GattDisconnected -> startReconnect(s.device)
        is Event.Failure -> ConnectionState.Error(e.err) to listOf(Action.Disconnect)
        Event.Disconnect -> ConnectionState.Disconnected to listOf(Action.Disconnect)
        else -> noop()
    }

    private fun handleConnected(
        s: ConnectionState.Connected,
        e: Event,
    ): Pair<ConnectionState, List<Action>> = when (e) {
        is Event.GattDisconnected -> startReconnect(s.device)
        is Event.Failure -> ConnectionState.Error(e.err) to listOf(Action.Disconnect)
        Event.Disconnect -> ConnectionState.Disconnected to listOf(Action.Disconnect)
        else -> noop()
    }

    private fun handleReconnecting(
        s: ConnectionState.Reconnecting,
        e: Event,
    ): Pair<ConnectionState, List<Action>> = when (e) {
        Event.BackoffElapsed -> {
            val device = lastDevice
            if (device == null) ConnectionState.Disconnected to emptyList()
            else ConnectionState.Connecting(device) to listOf(Action.Connect(device))
        }
        Event.Disconnect -> {
            attempt = 0
            ConnectionState.Disconnected to listOf(Action.Disconnect)
        }
        else -> noop()
    }

    private fun handleStray(e: Event): Pair<ConnectionState, List<Action>> = when (e) {
        Event.Disconnect -> ConnectionState.Disconnected to emptyList()
        is Event.Connect -> {
            lastDevice = e.device
            attempt = 0
            ConnectionState.Connecting(e.device) to listOf(Action.Connect(e.device))
        }
        else -> noop()
    }

    private fun startReconnect(device: BluetoothDevice): Pair<ConnectionState, List<Action>> {
        lastDevice = device
        attempt += 1
        return if (attempt > maxReconnectAttempts) {
            attempt = 0
            ConnectionState.Error(TunnelchatError.BleDisconnected) to listOf(Action.Disconnect)
        } else {
            ConnectionState.Reconnecting(attempt) to listOf(
                Action.ScheduleReconnect(backoffMs(attempt), attempt),
            )
        }
    }

    private fun noop(): Pair<ConnectionState, List<Action>> = _state to emptyList()

    companion object {
        const val DEFAULT_MAX_RECONNECT = 5

        /** Exponential backoff: 500ms, 1s, 2s, 4s, 8s, capped at 8s. */
        fun defaultBackoff(attempt: Int): Long {
            val n = (attempt - 1).coerceAtLeast(0).coerceAtMost(4)
            return 500L shl n
        }
    }
}
