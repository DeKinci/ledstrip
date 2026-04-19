package io.tunnelchat.api

enum class Presence { Online, Stale, Offline }

data class PeerInfo(
    val senderId: SenderId,
    val presence: Presence,
    val lastHeardMs: Long,
    val highSeq: Seq,
    val lastLocation: Message.Location?,
)

data class SelfInfo(
    val deviceId: UByte,
    val clockUnix: UInt,
    val activeSenders: UByte,
    val bootCount: UInt,
)
