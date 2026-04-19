package io.tunnelchat.api

import android.bluetooth.BluetoothDevice

sealed class ConnectionState {
    data object Disconnected : ConnectionState()
    data object Scanning : ConnectionState()
    data class Connecting(val device: BluetoothDevice) : ConnectionState()
    data class Connected(val device: BluetoothDevice, val mtu: Int) : ConnectionState()
    data class Reconnecting(val attempt: Int) : ConnectionState()
    data class Error(val err: TunnelchatError) : ConnectionState()
}
