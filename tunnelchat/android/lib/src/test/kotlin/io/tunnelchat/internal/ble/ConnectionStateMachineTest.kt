package io.tunnelchat.internal.ble

import android.bluetooth.BluetoothDevice
import io.tunnelchat.api.ConnectionState
import io.tunnelchat.api.TunnelchatError
import io.tunnelchat.internal.ble.ConnectionStateMachine.Action
import io.tunnelchat.internal.ble.ConnectionStateMachine.Event
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

/**
 * The FSM is pure Kotlin, but `BluetoothDevice` is `final` and can only be constructed
 * via reflection from a Bluetooth adapter. Robolectric's shadow Bluetooth stack is the
 * cheapest way to obtain a real instance for type-only carrying through the FSM.
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33])
class ConnectionStateMachineTest {

    private fun device(addr: String = "AA:BB:CC:DD:EE:FF"): BluetoothDevice {
        val adapter = android.bluetooth.BluetoothAdapter.getDefaultAdapter()
            ?: error("Robolectric Bluetooth adapter unavailable")
        return adapter.getRemoteDevice(addr)
    }

    @Test
    fun connect_disconnected_to_connecting_emits_connect_action() {
        val fsm = ConnectionStateMachine()
        val d = device()
        val tx = fsm.process(Event.Connect(d))
        assertEquals(ConnectionState.Connecting(d), tx.state)
        assertEquals(listOf(Action.Connect(d)), tx.actions)
    }

    @Test
    fun connecting_progresses_via_gatt_services_mtu_to_connected() {
        val fsm = ConnectionStateMachine()
        val d = device()
        fsm.process(Event.Connect(d))
        val gc = fsm.process(Event.GattConnected)
        assertEquals(ConnectionState.Connecting(d), gc.state)
        assertTrue(Action.RequestMtu in gc.actions && Action.DiscoverServices in gc.actions)

        val sd = fsm.process(Event.ServicesDiscovered)
        assertEquals(listOf(Action.EnableNotifications), sd.actions)

        val mtu = fsm.process(Event.MtuChanged(247))
        assertEquals(ConnectionState.Connected(d, 247), mtu.state)
    }

    @Test
    fun gatt_disconnect_from_connected_starts_reconnect() {
        val fsm = ConnectionStateMachine(maxReconnectAttempts = 3)
        val d = device()
        fsm.process(Event.Connect(d))
        fsm.process(Event.GattConnected); fsm.process(Event.ServicesDiscovered); fsm.process(Event.MtuChanged(247))

        val tx = fsm.process(Event.GattDisconnected(0x13))
        assertEquals(ConnectionState.Reconnecting(1), tx.state)
        val sched = tx.actions.single() as Action.ScheduleReconnect
        assertEquals(1, sched.attempt)
        assertTrue(sched.delayMs > 0)
    }

    @Test
    fun backoff_elapsed_re_enters_connecting_with_last_device() {
        val fsm = ConnectionStateMachine()
        val d = device()
        fsm.process(Event.Connect(d))
        fsm.process(Event.GattConnected); fsm.process(Event.MtuChanged(100))
        fsm.process(Event.GattDisconnected(0x08))

        val tx = fsm.process(Event.BackoffElapsed)
        assertEquals(ConnectionState.Connecting(d), tx.state)
        assertEquals(listOf(Action.Connect(d)), tx.actions)
    }

    @Test
    fun reconnect_attempts_exhausted_emits_error_and_disconnect() {
        val fsm = ConnectionStateMachine(maxReconnectAttempts = 2, backoffMs = { 0L })
        val d = device()
        fsm.process(Event.Connect(d)); fsm.process(Event.GattConnected); fsm.process(Event.MtuChanged(100))

        // Attempt 1
        fsm.process(Event.GattDisconnected(0x08))
        fsm.process(Event.BackoffElapsed)
        // Attempt 2
        fsm.process(Event.GattDisconnected(0x08))
        fsm.process(Event.BackoffElapsed)
        // Third disconnect → exceeds max → Error
        val tx = fsm.process(Event.GattDisconnected(0x08))
        assertEquals(ConnectionState.Error(TunnelchatError.BleDisconnected), tx.state)
        assertEquals(listOf(Action.Disconnect), tx.actions)
    }

    @Test
    fun explicit_disconnect_from_connected_clears_state() {
        val fsm = ConnectionStateMachine()
        val d = device()
        fsm.process(Event.Connect(d)); fsm.process(Event.GattConnected); fsm.process(Event.MtuChanged(100))
        val tx = fsm.process(Event.Disconnect)
        assertEquals(ConnectionState.Disconnected, tx.state)
        assertEquals(listOf(Action.Disconnect), tx.actions)
    }

    @Test
    fun explicit_disconnect_from_reconnecting_resets_attempts() {
        val fsm = ConnectionStateMachine(backoffMs = { 0L })
        val d = device()
        fsm.process(Event.Connect(d)); fsm.process(Event.GattConnected); fsm.process(Event.MtuChanged(100))
        fsm.process(Event.GattDisconnected(0x08))
        fsm.process(Event.Disconnect)
        // After disconnect, reconnecting again from scratch should yield attempt=1.
        fsm.process(Event.Connect(d))
        fsm.process(Event.GattConnected); fsm.process(Event.MtuChanged(100))
        val tx = fsm.process(Event.GattDisconnected(0x08))
        assertEquals(ConnectionState.Reconnecting(1), tx.state)
    }

    @Test
    fun stray_events_in_disconnected_are_ignored() {
        val fsm = ConnectionStateMachine()
        val tx = fsm.process(Event.GattDisconnected(0x08))
        assertEquals(ConnectionState.Disconnected, tx.state)
        assertTrue(tx.actions.isEmpty())
    }

    @Test
    fun default_backoff_is_exponential_capped() {
        assertEquals(500L, ConnectionStateMachine.defaultBackoff(1))
        assertEquals(1000L, ConnectionStateMachine.defaultBackoff(2))
        assertEquals(2000L, ConnectionStateMachine.defaultBackoff(3))
        assertEquals(4000L, ConnectionStateMachine.defaultBackoff(4))
        assertEquals(8000L, ConnectionStateMachine.defaultBackoff(5))
        assertEquals(8000L, ConnectionStateMachine.defaultBackoff(99))
    }
}
