#ifndef HTTP_RESPONSE_WRITER_H
#define HTTP_RESPONSE_WRITER_H

#include <WiFiClient.h>
#include "HttpResponse.h"

class HttpResponseWriter {
   public:
    static void write(WiFiClient& client, const HttpResponse& response);

   private:
    static const char* statusText(int code);
};

#endif  // HTTP_RESPONSE_WRITER_H