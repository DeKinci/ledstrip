package io.tunnelchat.internal.wire

import io.tunnelchat.api.Message
import io.tunnelchat.api.Presence
import io.tunnelchat.api.SenderId
import io.tunnelchat.api.Seq

/**
 * Decoders for BLE device→app notifications. All frames are:
 *
 *   [resp:1][payload:…]
 *
 * Source of truth: `device/retranslator/src/ble_cmd.h` + `bleGetState()` /
 * `bleGetSelfInfo()` / `blePushMessage()` in `src/relay.cpp`.
 *
 * Pure functions. Return `null` (or a typed [Notification.Malformed]) on any length
 * / range violation; callers log and discard — the BLE transport will push another.
 */
object NotificationFrame {

    const val RESP_STATE: Byte = 0x80.toByte()
    const val RESP_MESSAGE: Byte = 0x81.toByte()
    const val NOTIFY_INCOMING: Byte = 0x82.toByte()
    const val NOTIFY_PRESENCE: Byte = 0x83.toByte()
    const val RESP_SELF_INFO: Byte = 0x84.toByte()

    private const val MSG_TYPE_LOCATION: Int = 0x01
    private const val MSG_TYPE_TEXT: Int = 0x02

    fun decode(frame: ByteArray): Notification {
        if (frame.isEmpty()) return Notification.Malformed("empty frame")
        return when (frame[0]) {
            RESP_STATE -> decodeState(frame)
            RESP_MESSAGE -> decodeMessage(frame, push = false)
            NOTIFY_INCOMING -> decodeMessage(frame, push = true)
            NOTIFY_PRESENCE -> decodePresence(frame)
            RESP_SELF_INFO -> decodeSelfInfo(frame)
            else -> Notification.Malformed("unknown resp=0x%02x".format(frame[0].toInt() and 0xFF))
        }
    }

    private fun decodeState(f: ByteArray): Notification {
        if (f.size < 2) return Notification.Malformed("state: too short")
        val count = f[1].toInt() and 0xFF
        val expected = 2 + count * 8
        if (f.size < expected) return Notification.Malformed("state: expected $expected, got ${f.size}")
        val entries = ArrayList<StateEntry>(count)
        var p = 2
        repeat(count) {
            entries += StateEntry(
                senderId = SenderId(f[p].toUByte()),
                highSeq = Seq(readU16Be(f, p + 1)),
                locSeq = Seq(readU16Be(f, p + 3)),
                nodeA = f[p + 5].toUByte(),
                nodeB = f[p + 6].toUByte(),
                presence = presenceOf(f[p + 7].toInt() and 0xFF)
                    ?: return Notification.Malformed("state: bad presence byte"),
            )
            p += 8
        }
        return Notification.StateResp(entries)
    }

    private fun decodeMessage(f: ByteArray, push: Boolean): Notification {
        // [resp][senderId:1][seq:2][ts:4][type:1][payload:N]
        if (f.size < 9) return Notification.Malformed("message: too short (${f.size})")
        val senderId = SenderId(f[1].toUByte())
        val seq = Seq(readU16Be(f, 2))
        val ts = readU32Be(f, 4)
        val msgType = f[8].toInt() and 0xFF
        val payload = f.copyOfRange(9, f.size)
        val msg = decodePayload(msgType, payload)
            ?: return Notification.Malformed("message: bad payload for type=0x%02x".format(msgType))
        return Notification.MessageFrame(
            push = push,
            senderId = senderId,
            seq = seq,
            timestamp = ts,
            message = msg,
        )
    }

    private fun decodePayload(msgType: Int, payload: ByteArray): Message? = when (msgType) {
        MSG_TYPE_LOCATION -> if (payload.size >= 2) {
            Message.Location(payload[0].toUByte(), payload[1].toUByte())
        } else null

        MSG_TYPE_TEXT -> if (payload.isNotEmpty()) {
            val len = payload[0].toInt() and 0xFF
            if (payload.size < 1 + len) null
            else Message.Text(payload.copyOfRange(1, 1 + len))
        } else null

        else -> Message.Opaque(msgType.toUByte(), payload)
    }

    private fun decodePresence(f: ByteArray): Notification {
        if (f.size < 3) return Notification.Malformed("presence: too short")
        val presence = presenceOf(f[2].toInt() and 0xFF)
            ?: return Notification.Malformed("presence: bad byte=${f[2].toInt() and 0xFF}")
        return Notification.PresenceFrame(SenderId(f[1].toUByte()), presence)
    }

    private fun decodeSelfInfo(f: ByteArray): Notification {
        // [resp][deviceId:1][clock:4][activeSenders:1][bootCount:4] = 11 bytes
        if (f.size < 11) return Notification.Malformed("selfInfo: too short (${f.size})")
        return Notification.SelfInfoResp(
            deviceId = f[1].toUByte(),
            clockUnix = readU32Be(f, 2),
            activeSenders = f[6].toUByte(),
            bootCount = readU32Be(f, 7),
        )
    }

    private fun presenceOf(byte: Int): Presence? = when (byte) {
        0 -> Presence.Online
        1 -> Presence.Stale
        2 -> Presence.Offline
        else -> null
    }
}

data class StateEntry(
    val senderId: SenderId,
    val highSeq: Seq,
    val locSeq: Seq,
    val nodeA: UByte,
    val nodeB: UByte,
    val presence: Presence,
)

sealed class Notification {
    data class StateResp(val entries: List<StateEntry>) : Notification()
    data class MessageFrame(
        /** True for 0x82 IncomingMsg push, false for 0x81 MessageResp reply. */
        val push: Boolean,
        val senderId: SenderId,
        val seq: Seq,
        val timestamp: UInt,
        val message: Message,
    ) : Notification()
    data class PresenceFrame(val senderId: SenderId, val presence: Presence) : Notification()
    data class SelfInfoResp(
        val deviceId: UByte,
        val clockUnix: UInt,
        val activeSenders: UByte,
        val bootCount: UInt,
    ) : Notification()
    data class Malformed(val reason: String) : Notification()
}
