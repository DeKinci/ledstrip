#include "GatewayClient.h"
#include "GatewayUrl.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <MicroLog.h>
#include <PropertySystem.h>
#include <Property.h>
#include <ListProperty.h>
#include <MicroProtoController.h>
#include <messages/Hello.h>
#include <wire/OpCode.h>

static const char* TAG = "Gateway";

// Gateway config properties
static MicroProto::StringProperty<128> gatewayUrl("gatewayUrl",
    MicroProto::PropertyLevel::LOCAL,
    "Gateway WebSocket URL",
    MicroProto::UIHints().setColor(MicroProto::UIColor::SKY).setIcon("🌐"),
    true);

static MicroProto::StringProperty<64> gatewayToken("gatewayToken",
    MicroProto::PropertyLevel::LOCAL,
    "Gateway registration token",
    MicroProto::UIHints().setColor(MicroProto::UIColor::SLATE).setIcon("🔑"),
    true);

static MicroProto::StringProperty<32> deviceName("deviceName",
    MicroProto::PropertyLevel::LOCAL,
    "Device display name",
    MicroProto::UIHints().setColor(MicroProto::UIColor::EMERALD).setIcon("📛"),
    true);

namespace {

using namespace MicroProto;

/**
 * GatewayTransport — outbound WebSocket to a remote gateway.
 * Implements MicroProtoTransport so the controller handles all protocol logic.
 * Single client (the gateway itself acts as a multiplexing proxy).
 */
class GatewayTransport : public MicroProtoTransport {
public:
    void send(uint8_t /*localClientId*/, const uint8_t* data, size_t len) override {
        if (_connected) _ws.sendBIN(data, len);
    }

    uint8_t maxClients() const override { return 1; }

    bool isClientConnected(uint8_t /*localClientId*/) const override {
        return _connected;
    }

    uint32_t maxPacketSize(uint8_t /*localClientId*/) const override {
        return 4096;
    }

    WebSocketsClient _ws;
    bool _connected = false;
};

GatewayTransport gwTransport;
MicroProtoController* gwController = nullptr;
uint8_t gwClientIdOffset = 0;
bool enabled = false;

String getDeviceId() {
    uint64_t mac = ESP.getEfuseMac();
    char id[13];
    snprintf(id, sizeof(id), "%04x%08x",
             static_cast<uint16_t>(mac >> 32),
             static_cast<uint32_t>(mac));
    return String(id);
}

String getDeviceName() {
    char buf[32];
    if (deviceName.getString(buf, sizeof(buf)) > 0 && buf[0]) return String(buf);
    return "LED-" + getDeviceId().substring(8);
}

String getUrl() {
    char buf[128];
    if (gatewayUrl.getString(buf, sizeof(buf)) > 0 && buf[0]) return String(buf);
#ifdef GATEWAY_URL
    return String(GATEWAY_URL);
#else
    return "";
#endif
}

String getToken() {
    char buf[64];
    if (gatewayToken.getString(buf, sizeof(buf)) > 0 && buf[0]) return String(buf);
#ifdef GATEWAY_TOKEN
    return String(GATEWAY_TOKEN);
#else
    return "";
#endif
}

// Send idle HELLO to register with gateway without full sync
void sendIdleHello() {
    uint64_t mac = ESP.getEfuseMac();
    uint32_t devId = static_cast<uint32_t>(mac);
    Hello hello = Hello::idleRequest(devId);
    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));
    if (hello.encode(wb)) {
        gwTransport._ws.sendBIN(buf, wb.position());
        LOG_INFO(TAG, "Sent idle HELLO to gateway");
    }
}

void onWsEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            gwTransport._connected = true;
            LOG_INFO(TAG, "Connected to gateway");
            sendIdleHello();
            break;

        case WStype_DISCONNECTED:
            gwTransport._connected = false;
            gwController->onClientDisconnected(gwClientIdOffset);
            LOG_INFO(TAG, "Disconnected from gateway");
            break;

        case WStype_BIN:
            // All messages go through the controller — HELLO, PROPERTY_UPDATE,
            // RESOURCE_GET/PUT/DELETE, RPC, PING, etc.
            gwController->processMessage(gwClientIdOffset, payload, length);
            break;

        default:
            break;
    }
}

} // namespace

namespace GatewayClient {

void init(MicroProto::MicroProtoController* controller) {
    gwController = controller;

    String url = getUrl();
    if (url.isEmpty()) {
        LOG_INFO(TAG, "No gateway URL configured");
        enabled = false;
        return;
    }

    // Register transport with controller
    gwClientIdOffset = controller->registerTransport(&gwTransport);

    String token = getToken();
    String devId = getDeviceId();
    String devName = getDeviceName();

    GatewayClient::ParsedUrl parsed;
    if (!GatewayClient::parseUrl(url, parsed)) {
        LOG_ERROR(TAG, "Invalid gateway URL: %s", url.c_str());
        enabled = false;
        return;
    }

    parsed.path += "?token=" + token + "&id=" + devId + "&name=" + devName
                 + "&ip=" + WiFi.localIP().toString();

    LOG_INFO(TAG, "Gateway: %s:%d (ssl=%d)", parsed.host.c_str(), parsed.port, parsed.ssl);

    if (parsed.ssl) {
        gwTransport._ws.beginSSL(parsed.host, parsed.port, parsed.path);
    } else {
        gwTransport._ws.begin(parsed.host, parsed.port, parsed.path);
    }
    gwTransport._ws.onEvent(onWsEvent);
    gwTransport._ws.setReconnectInterval(5000);

    enabled = true;
}

void loop() {
    if (!enabled) return;
    gwTransport._ws.loop();
}

bool isConnected() {
    return gwTransport._connected;
}

} // namespace GatewayClient
