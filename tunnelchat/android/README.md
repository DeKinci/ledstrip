# tunnelchat/android

Android Kotlin library for the tunnelchat underground mesh. See `DESIGN.md` for architecture.

## Modules

- `lib/` — the library (`io.tunnelchat`), published as an AAR.
- `demo/` — minimal Activity for manual testing against a paired retranslator.

## Build

Requires JDK 17 and Android SDK (with `ANDROID_HOME` or `ANDROID_SDK_ROOT` set).

First time (generates the Gradle wrapper JAR):

```bash
gradle wrapper --gradle-version 8.10.2
```

Then:

```bash
./gradlew :lib:assembleDebug
./gradlew :demo:assembleDebug
```

## Package layout

```
io.tunnelchat            (public API: Tunnelchat, config, types)
io.tunnelchat.api        (public data classes)
io.tunnelchat.internal.* (ble, wire, protocol, blob, proto, archive, stats, log)
```

## Permissions

App declares `BLUETOOTH_SCAN` and `BLUETOOTH_CONNECT` (API 31+) and requests them at runtime. The library returns `TunnelchatError.PermissionMissing` on calls that need them.
