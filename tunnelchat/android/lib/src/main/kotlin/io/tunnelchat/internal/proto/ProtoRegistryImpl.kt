package io.tunnelchat.internal.proto

import com.google.protobuf.MessageLite
import com.google.protobuf.Parser
import io.tunnelchat.api.ProtoRegistry
import java.util.concurrent.ConcurrentHashMap

/**
 * Backs [ProtoRegistry]. Library built-in IDs (0..255) are reserved; apps using the
 * public `register` API must pick IDs >= 256.
 */
internal class ProtoRegistryImpl : ProtoRegistry {
    private val parsers = ConcurrentHashMap<UShort, Parser<out MessageLite>>()

    override fun <T : MessageLite> register(schemaId: UShort, parser: Parser<T>) {
        require(schemaId.toInt() >= RESERVED_MAX_EXCL) {
            "schemaId ${schemaId.toInt()} is reserved for library built-ins (0..${RESERVED_MAX_EXCL - 1})"
        }
        parsers[schemaId] = parser
    }

    override fun unregister(schemaId: UShort) {
        parsers.remove(schemaId)
    }

    override fun isRegistered(schemaId: UShort): Boolean = parsers.containsKey(schemaId)

    internal fun parserFor(schemaId: UShort): Parser<out MessageLite>? = parsers[schemaId]

    companion object {
        /** First app-available schema ID. IDs 0..255 are reserved for library built-ins. */
        const val RESERVED_MAX_EXCL: Int = 256
    }
}
