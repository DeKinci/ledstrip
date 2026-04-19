package io.tunnelchat.api

import com.google.protobuf.MessageLite
import com.google.protobuf.Parser

/**
 * Registry of app-defined proto schemas.
 *
 * Library-reserved IDs are 0..255 (Echo=1, Pong=2 today). Apps must use 256..65535.
 */
interface ProtoRegistry {
    fun <T : MessageLite> register(schemaId: UShort, parser: Parser<T>)
    fun unregister(schemaId: UShort)
    fun isRegistered(schemaId: UShort): Boolean
}

sealed class ProtoArrival {
    abstract val senderId: SenderId
    abstract val schemaId: UShort

    data class Known(
        override val senderId: SenderId,
        override val schemaId: UShort,
        val message: MessageLite,
    ) : ProtoArrival()

    data class Unknown(
        override val senderId: SenderId,
        override val schemaId: UShort,
        val rawBytes: ByteArray,
    ) : ProtoArrival() {
        override fun equals(other: Any?): Boolean =
            other is Unknown &&
                senderId == other.senderId &&
                schemaId == other.schemaId &&
                rawBytes.contentEquals(other.rawBytes)
        override fun hashCode(): Int =
            31 * (31 * senderId.hashCode() + schemaId.hashCode()) + rawBytes.contentHashCode()
    }
}

data class EchoResult(val rttMs: Long, val pingId: UInt)
