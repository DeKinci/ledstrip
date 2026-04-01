#pragma once

#include <Arduino.h>

namespace GatewayClient {

struct ParsedUrl {
    String host;
    uint16_t port = 0;
    String path;
    bool ssl = false;
};

/**
 * Parse a gateway WebSocket URL into components.
 * Supports ws://, wss://, http://, https:// schemes.
 * Defaults: path="/ws/device", port=80 (or 443 for SSL).
 * Returns false if host is empty.
 */
inline bool parseUrl(const String& url, ParsedUrl& out) {
    String u = url;
    out.ssl = false;

    if (u.startsWith("wss://") || u.startsWith("https://")) {
        out.ssl = true;
        u = u.substring(u.indexOf("://") + 3);
    } else if (u.startsWith("ws://") || u.startsWith("http://")) {
        u = u.substring(u.indexOf("://") + 3);
    }

    int pathIdx = u.indexOf('/');
    String hostPort;
    if (pathIdx >= 0) {
        hostPort = u.substring(0, pathIdx);
        out.path = u.substring(pathIdx);
    } else {
        hostPort = u;
        out.path = "/ws/device";
    }

    int colonIdx = hostPort.indexOf(':');
    if (colonIdx >= 0) {
        out.host = hostPort.substring(0, colonIdx);
        out.port = hostPort.substring(colonIdx + 1).toInt();
    } else {
        out.host = hostPort;
        out.port = out.ssl ? 443 : 80;
    }

    return out.host.length() > 0;
}

} // namespace GatewayClient
