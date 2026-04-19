package io.tunnelchat.internal.ble

import java.util.UUID

/**
 * Nordic UART Service (NUS), as exposed by the retranslator firmware
 * (`device/retranslator/include/config.h`).
 *
 * From the phone's perspective:
 * - We **write** outbound commands to [NUS_RX_CHAR] (firmware's RX).
 * - We **subscribe to notifications** on [NUS_TX_CHAR] (firmware's TX).
 */
internal object BleConstants {
    val NUS_SERVICE: UUID = UUID.fromString("6e400001-b5a3-f393-e0a9-e50e24dcca9e")
    val NUS_RX_CHAR: UUID = UUID.fromString("6e400002-b5a3-f393-e0a9-e50e24dcca9e")
    val NUS_TX_CHAR: UUID = UUID.fromString("6e400003-b5a3-f393-e0a9-e50e24dcca9e")
    val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    /** Highest MTU we'll request. Firmware will negotiate down. */
    const val DESIRED_MTU = 247
}
