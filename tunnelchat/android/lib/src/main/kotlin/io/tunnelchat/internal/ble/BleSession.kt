package io.tunnelchat.internal.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.content.Context
import android.os.Build
import io.tunnelchat.api.ConnectionState
import io.tunnelchat.internal.protocol.CommandTransport
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.consumeAsFlow

/**
 * One BLE link to one retranslator. Owns the [BluetoothGatt] handle, the dedicated
 * BLE handler thread (via [GattCallbackBridge]), and the [ConnectionStateMachine].
 *
 * Public surface (within the library):
 * - [state]: hot state stream surfaced to `Tunnelchat.connectionState`.
 * - [inbound]: every payload received on the NUS TX characteristic, exactly once.
 * - [send]: enqueue an outbound frame. Returns false if not connected.
 * - [connect]/[disconnect]/[close].
 *
 * Bluetooth permissions are the caller's problem — we only catch [SecurityException]
 * around the Android calls so the FSM transitions to a clean error state.
 */
@SuppressLint("MissingPermission")
internal class BleSession(
    private val appContext: Context,
    private val fsm: ConnectionStateMachine = ConnectionStateMachine(),
) : CommandTransport {
    private val _state = MutableStateFlow<ConnectionState>(ConnectionState.Disconnected)
    val state: StateFlow<ConnectionState> = _state.asStateFlow()

    private val inboundChan = Channel<ByteArray>(capacity = Channel.BUFFERED)
    override val inbound: Flow<ByteArray> = inboundChan.consumeAsFlow()

    private val bridge = GattCallbackBridge(
        onEvent = ::dispatch,
        onInbound = { bytes -> inboundChan.trySend(bytes) },
    )

    private var gatt: BluetoothGatt? = null
    private var rxChar: BluetoothGattCharacteristic? = null

    fun connect(device: BluetoothDevice) {
        bridge.post { dispatch(ConnectionStateMachine.Event.Connect(device)) }
    }

    fun disconnect() {
        bridge.post { dispatch(ConnectionStateMachine.Event.Disconnect) }
    }

    /** Best-effort enqueue. Returns false when the link isn't [ConnectionState.Connected]. */
    override fun send(frame: ByteArray): Boolean {
        val g = gatt ?: return false
        val ch = rxChar ?: return false
        if (_state.value !is ConnectionState.Connected) return false
        return try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                g.writeCharacteristic(ch, frame, BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE) ==
                    BluetoothGatt.GATT_SUCCESS
            } else {
                @Suppress("DEPRECATION")
                ch.value = frame
                @Suppress("DEPRECATION")
                ch.writeType = BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
                @Suppress("DEPRECATION")
                g.writeCharacteristic(ch)
            }
        } catch (_: SecurityException) {
            false
        }
    }

    fun close() {
        bridge.post {
            try { gatt?.close() } catch (_: SecurityException) {}
            gatt = null
            rxChar = null
        }
        bridge.close()
        inboundChan.close()
    }

    private fun dispatch(event: ConnectionStateMachine.Event) {
        val tx = fsm.process(event)
        _state.value = tx.state
        tx.actions.forEach(::execute)
    }

    private fun execute(action: ConnectionStateMachine.Action) {
        when (action) {
            is ConnectionStateMachine.Action.Connect -> openGatt(action.device)
            ConnectionStateMachine.Action.Disconnect -> closeGatt()
            ConnectionStateMachine.Action.RequestMtu -> {
                try { gatt?.requestMtu(BleConstants.DESIRED_MTU) } catch (_: SecurityException) {}
            }
            ConnectionStateMachine.Action.DiscoverServices -> {
                try { gatt?.discoverServices() } catch (_: SecurityException) {}
            }
            ConnectionStateMachine.Action.EnableNotifications -> enableNotifications()
            is ConnectionStateMachine.Action.ScheduleReconnect -> {
                bridge.postDelayed(action.delayMs) {
                    dispatch(ConnectionStateMachine.Event.BackoffElapsed)
                }
            }
        }
    }

    private fun openGatt(device: BluetoothDevice) {
        try {
            gatt = device.connectGatt(appContext, false, bridge.callback, BluetoothDevice.TRANSPORT_LE)
        } catch (_: SecurityException) {
            dispatch(ConnectionStateMachine.Event.GattDisconnected(0xFF))
        }
    }

    private fun closeGatt() {
        try { gatt?.disconnect() } catch (_: SecurityException) {}
        try { gatt?.close() } catch (_: SecurityException) {}
        gatt = null
        rxChar = null
    }

    @Suppress("DEPRECATION")
    private fun enableNotifications() {
        val g = gatt ?: return
        val service = g.getService(BleConstants.NUS_SERVICE) ?: return
        val tx = service.getCharacteristic(BleConstants.NUS_TX_CHAR) ?: return
        rxChar = service.getCharacteristic(BleConstants.NUS_RX_CHAR)
        try {
            g.setCharacteristicNotification(tx, true)
            val cccd = tx.getDescriptor(BleConstants.CCCD) ?: return
            val enable = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                g.writeDescriptor(cccd, enable)
            } else {
                cccd.value = enable
                g.writeDescriptor(cccd)
            }
        } catch (_: SecurityException) {}
    }
}
