#include "WebServer.h"
#include "ApiController.h"
#include "SocketController.h"
#include "core/ShaderStorage.h"
#include "rsc/w_index_htm.h"
#include "rsc/w_ble_htm.h"

#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>

void WebServer::init() {
    // WiFi setup will be done in main
    // This is just for any global initialization
    Serial.println("WebServer: init");
}

void WebServer::begin(WiFiServer& server, WebSocketsServer& ws) {
    server.begin();
    ws.begin();
    ws.onEvent(SocketController::onWebSocketEvent);
    SocketController::setWebSocket(&ws);  // Pass WebSocket instance to SocketController

    Serial.println("✓ HTTP server started on port 80");
    Serial.println("✓ WebSocket server started on port 81");
}

void WebServer::loop(WiFiServer& server, WebSocketsServer& ws) {
    // CRITICAL: Handle WebSocket first
    ws.loop();

    // Check for HTTP clients
    WiFiClient client = server.accept();  // Use accept() instead of deprecated available()
    if (client) {
        handleClient(client, ws);
    }
}

void WebServer::handleClient(WiFiClient& client, WebSocketsServer& ws) {
    uint32_t start = millis();
    String request = "";
    bool headerComplete = false;

    // Read request headers with frequent WebSocket loop calls
    while (client.connected() && millis() - start < 1000 && !headerComplete) {
        ws.loop();  // Keep WebSocket responsive

        if (client.available()) {
            char c = client.read();
            request += c;

            if (request.endsWith("\r\n\r\n")) {
                headerComplete = true;
            }
        } else {
            delay(1);
        }
    }

    if (!headerComplete) {
        client.stop();
        return;
    }

    // Parse request line
    int firstSpace = request.indexOf(' ');
    int secondSpace = request.indexOf(' ', firstSpace + 1);

    if (firstSpace == -1 || secondSpace == -1) {
        sendResponse(client, 400, "text/plain", "Bad Request");
        client.stop();
        return;
    }

    String method = request.substring(0, firstSpace);
    String path = request.substring(firstSpace + 1, secondSpace);

    // Read body if Content-Length present
    String body = "";
    int bodyStart = request.indexOf("\r\n\r\n") + 4;
    if (bodyStart > 3) {
        body = request.substring(bodyStart);

        // Check if we need to read more body data
        int clIdx = request.indexOf("Content-Length:");
        if (clIdx != -1) {
            int clEnd = request.indexOf("\r\n", clIdx);
            int contentLength = request.substring(clIdx + 15, clEnd).toInt();

            // Read remaining body
            while (body.length() < contentLength && millis() - start < 2000) {
                ws.loop();
                if (client.available()) {
                    body += (char)client.read();
                } else {
                    delay(1);
                }
            }
        }
    }

    Serial.printf("[HTTP] %s %s\n", method.c_str(), path.c_str());

    // Route handling - simple if/else chain
    if (method == "GET" && path == "/ping") {
        sendResponse(client, 200, "text/plain", "pong");
    }
    else if (method == "GET" && path == "/index") {
        // Send raw HTML (not gzipped)
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/html");
        client.printf("Content-Length: %d\r\n", index_htm_len);
        client.println("Connection: close");
        client.println();
        client.write(index_htm, index_htm_len);
    }
    else if (method == "GET" && path == "/ble") {
        // Send raw HTML (not gzipped)
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/html");
        client.printf("Content-Length: %d\r\n", ble_htm_len);
        client.println("Connection: close");
        client.println();
        client.write(ble_htm, ble_htm_len);
    }
    else if (method == "GET" && path == "/nuke") {
        sendResponse(client, 200, "text/plain", "Formatting SPIFFS and rebooting...");
        delay(100);
        ShaderStorage::get().nuke();
    }
    else if (method == "GET" && path == "/api/shader") {
        ApiController::onListShaders(client);
    }
    else if (method == "POST" && path == "/api/shader") {
        ApiController::onAddShader(client, body);
    }
    else if (method == "GET" && path.startsWith("/api/shader/")) {
        String name = path.substring(13);
        ApiController::onGetShader(client, name);
    }
    else if (method == "DELETE" && path.startsWith("/api/shader/")) {
        String name = path.substring(13);
        ApiController::onDeleteShader(client, name);
    }
    else if (method == "GET" && path.startsWith("/api/show/")) {
        String name = path.substring(10);
        ApiController::onShow(client, name);
    }
    else if (method == "GET" && path == "/api/show") {
        ApiController::onGetShow(client);
    }
    else if (method == "POST" && path == "/api/ble/scan") {
        ApiController::onBleScan(client);
    }
    else if (method == "GET" && path == "/api/ble/scan/results") {
        ApiController::onBleGetScanResults(client);
    }
    else if (method == "GET" && path == "/api/ble/known") {
        ApiController::onBleGetKnownDevices(client);
    }
    else if (method == "POST" && path == "/api/ble/known") {
        ApiController::onBleAddDevice(client, body);
    }
    else if (method == "DELETE" && path.startsWith("/api/ble/known/")) {
        String address = path.substring(15);
        ApiController::onBleRemoveDevice(client, address);
    }
    else if (method == "GET" && path == "/api/ble/connected") {
        ApiController::onBleGetConnectedDevices(client);
    }
    else if (method == "POST" && path.startsWith("/api/ble/connect/")) {
        String address = path.substring(17);
        ApiController::onBleConnect(client, address);
    }
    else if (method == "POST" && path.startsWith("/api/ble/disconnect/")) {
        String address = path.substring(20);
        ApiController::onBleDisconnect(client, address);
    }
    else {
        sendResponse(client, 404, "text/plain", "Not Found");
    }

    client.stop();
}

void WebServer::sendResponse(WiFiClient& client, int code, const String& contentType, const String& body) {
    client.printf("HTTP/1.1 %d OK\r\n", code);
    client.printf("Content-Type: %s\r\n", contentType.c_str());
    client.printf("Content-Length: %d\r\n", body.length());
    client.println("Connection: close");
    client.println();
    client.print(body);
}

void WebServer::sendJson(WiFiClient& client, int code, const String& json) {
    sendResponse(client, code, "application/json", json);
}

void WebServer::sendGzip(WiFiClient& client, int code, const String& contentType, const uint8_t* data, size_t len) {
    client.printf("HTTP/1.1 %d OK\r\n", code);
    client.printf("Content-Type: %s\r\n", contentType.c_str());
    client.printf("Content-Length: %d\r\n", len);
    client.println("Content-Encoding: gzip");
    client.println("Connection: close");
    client.println();
    client.write(data, len);
}
