#ifndef HTTP_REQUEST_READER_H
#define HTTP_REQUEST_READER_H

#include <Arduino.h>
#include <WiFiClient.h>
#include "HttpRequest.h"
#include "RequestBuffer.h"

struct HttpReaderConfig {
    uint32_t firstByteTimeoutMs = 5000;  // Wait for client to start sending
    uint32_t readTimeoutMs = 500;        // Timeout for headers/body after first byte
    size_t maxBodySize = 8192;           // Max body size to read
};

class HttpRequestReader {
   public:
    // Read complete HTTP request into buffer, parse into HttpRequest
    // Buffer must outlive HttpRequest (request contains views into buffer)
    // On failure, sends appropriate error response (408/400/413)
    static bool read(WiFiClient& client, RequestBuffer& buffer, HttpRequest& req,
                     const HttpReaderConfig& config = HttpReaderConfig());

   private:
    static bool waitForData(WiFiClient& client, uint32_t timeoutMs);
    static bool readUntilTerminator(WiFiClient& client, RequestBuffer& buffer, uint32_t timeoutMs);
    static bool readBody(WiFiClient& client, RequestBuffer& buffer, size_t count, uint32_t timeoutMs);
    static int parseContentLength(const char* headers, size_t len);
};

#endif  // HTTP_REQUEST_READER_H