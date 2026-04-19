package io.tunnelchat.internal.ble

import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothProfile
import android.os.Build
import android.os.Handler
import android.os.HandlerThread

/**
 * Marshals [BluetoothGattCallback] events onto a single dedicated thread before invoking
 * [onEvent]. Required because `BluetoothGatt` callbacks come back on an internal binder
 * thread and must not be re-entered from arbitrary callers; serialising through one
 * handler thread avoids reordering and races.
 */
internal class GattCallbackBridge(
    private val onEvent: (ConnectionStateMachine.Event) -> Unit,
    private val onInbound: (ByteArray) -> Unit,
) {
    private val thread = HandlerThread("tunnelchat-ble").apply { start() }
    private val handler = Handler(thread.looper)

    val callback: BluetoothGattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            handler.post {
                if (newState == BluetoothProfile.STATE_CONNECTED && status == BluetoothGatt.GATT_SUCCESS) {
                    onEvent(ConnectionStateMachine.Event.GattConnected)
                } else {
                    onEvent(ConnectionStateMachine.Event.GattDisconnected(status))
                }
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            handler.post {
                if (status == BluetoothGatt.GATT_SUCCESS) {
                    onEvent(ConnectionStateMachine.Event.ServicesDiscovered)
                }
            }
        }

        override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
            handler.post {
                if (status == BluetoothGatt.GATT_SUCCESS) {
                    onEvent(ConnectionStateMachine.Event.MtuChanged(mtu))
                }
            }
        }

        @Deprecated("API < 33")
        override fun onCharacteristicChanged(gatt: BluetoothGatt, char: BluetoothGattCharacteristic) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) return
            val bytes = char.value ?: return
            handler.post { onInbound(bytes.copyOf()) }
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            char: BluetoothGattCharacteristic,
            value: ByteArray,
        ) {
            handler.post { onInbound(value.copyOf()) }
        }
    }

    /** Run [block] on the BLE handler thread. All GATT writes must go through here. */
    fun post(block: () -> Unit) {
        handler.post(block)
    }

    fun postDelayed(delayMs: Long, block: () -> Unit) {
        handler.postDelayed(block, delayMs)
    }

    fun close() {
        handler.removeCallbacksAndMessages(null)
        thread.quitSafely()
    }
}
