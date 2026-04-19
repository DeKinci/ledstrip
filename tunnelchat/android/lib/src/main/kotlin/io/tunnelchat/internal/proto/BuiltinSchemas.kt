package io.tunnelchat.internal.proto

/**
 * Library-reserved schema IDs. Assigned at the library layer (not in the .proto
 * file) so the public facade can advertise a clean namespace split: 0..255 library,
 * 256..65535 app.
 */
internal object BuiltinSchemas {
    const val ECHO: Int = 1
    const val PONG: Int = 2
}
