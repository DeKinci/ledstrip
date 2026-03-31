#include "HttpRequestReader.h"

bool HttpRequestReader::read(WiFiClient& client, RequestBuffer& buffer, HttpRequest& req,
                              const HttpReaderConfig& config) {
    buffer.reset();

    // Wait for first byte
    if (!waitForData(client, config.firstByteTimeoutMs)) {
        client.print("HTTP/1.1 408 Request Timeout\r\n\r\n");
        client.stop();
        return false;
    }

    // Read headers into buffer
    if (!readUntilTerminator(client, buffer, config.readTimeoutMs)) {
        client.print("HTTP/1.1 400 Bad Request\r\n\r\n");
        client.stop();
        return false;
    }

    size_t headerEnd = buffer.length();

    // Parse content-length from what we have so far
    int contentLength = parseContentLength(buffer.data(), headerEnd);

    // Read body if present
    if (contentLength > 0) {
        if ((size_t)contentLength > config.maxBodySize) {
            client.print("HTTP/1.1 413 Payload Too Large\r\n\r\n");
            client.stop();
            return false;
        }

        if (!readBody(client, buffer, contentLength, config.readTimeoutMs)) {
            client.print("HTTP/1.1 400 Bad Request\r\n\r\n");
            client.stop();
            return false;
        }
    }

    // Parse buffer into request
    if (!req.parse(buffer.data(), buffer.length())) {
        client.print("HTTP/1.1 400 Bad Request\r\n\r\n");
        client.stop();
        return false;
    }

    return true;
}

bool HttpRequestReader::waitForData(WiFiClient& client, uint32_t timeoutMs) {
    uint32_t start = millis();
    while (!client.available() && client.connected()) {
        if (millis() - start > timeoutMs) {
            return false;
        }
        delay(1);
    }
    return client.available() > 0;
}

bool HttpRequestReader::readUntilTerminator(WiFiClient& client, RequestBuffer& buffer, uint32_t timeoutMs) {
    uint32_t start = millis();

    while (millis() - start < timeoutMs) {
        while (client.available() && buffer.remaining() > 0) {
            char c = client.read();
            buffer.write(c);

            // Check for \r\n\r\n terminator
            size_t len = buffer.length();
            if (len >= 4 &&
                buffer[len-4] == '\r' && buffer[len-3] == '\n' &&
                buffer[len-2] == '\r' && buffer[len-1] == '\n') {
                return true;
            }
        }

        if (buffer.remaining() == 0) {
            return false;  // Buffer full, no terminator found
        }

        delay(1);
    }
    return false;
}

bool HttpRequestReader::readBody(WiFiClient& client, RequestBuffer& buffer, size_t count, uint32_t timeoutMs) {
    uint32_t start = millis();
    size_t bytesRead = 0;

    while (bytesRead < count && millis() - start < timeoutMs) {
        if (client.available()) {
            size_t toRead = count - bytesRead;
            if (toRead > buffer.remaining()) toRead = buffer.remaining();
            if (toRead == 0) return false;  // Buffer full

            size_t chunk = client.available();
            if (chunk > toRead) chunk = toRead;

            int actual = client.read((uint8_t*)buffer.writePtr(), chunk);
            if (actual > 0) {
                buffer.advance(actual);
                bytesRead += actual;
            }
        } else {
            delay(1);
        }
    }
    return bytesRead == count;
}

int HttpRequestReader::parseContentLength(const char* headers, size_t len) {
    // Case-insensitive search for "content-length:"
    const char* target = "content-length:";
    const size_t targetLen = 15;

    for (size_t i = 0; i + targetLen < len; i++) {
        bool match = true;
        for (size_t j = 0; j < targetLen && match; j++) {
            char c = headers[i + j];
            char t = target[j];
            if (c >= 'A' && c <= 'Z') c += 32;
            if (c != t) match = false;
        }

        if (match) {
            // Skip whitespace
            size_t valueStart = i + targetLen;
            while (valueStart < len && (headers[valueStart] == ' ' || headers[valueStart] == '\t')) {
                valueStart++;
            }

            // Parse number
            int value = 0;
            bool hasDigit = false;
            while (valueStart < len && headers[valueStart] >= '0' && headers[valueStart] <= '9') {
                hasDigit = true;
                int digit = headers[valueStart] - '0';
                // Overflow check
                if (value > (INT_MAX - digit) / 10) {
                    return -1;
                }
                value = value * 10 + digit;
                valueStart++;
            }

            return hasDigit ? value : -1;
        }
    }
    return 0;  // Not found = no body
}