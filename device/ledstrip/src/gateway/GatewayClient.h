#pragma once

#include <Arduino.h>
#include <transport/MicroProtoTransport.h>

// Forward declare
namespace MicroProto { class MicroProtoController; }

namespace GatewayClient {
    void init(MicroProto::MicroProtoController* controller);
    void loop();
    bool isConnected();
}
