#ifndef MICROPROTO_MESSAGES_MESSAGE_ROUTER_H
#define MICROPROTO_MESSAGES_MESSAGE_ROUTER_H

#include "../wire/Buffer.h"
#include "../wire/OpCode.h"
#include "../wire/PropertyUpdate.h"
#include "../PropertyBase.h"
#include "Hello.h"
#include "Error.h"

namespace MicroProto {

/**
 * MessageHandler - Callback interface for message handling
 */
class MessageHandler {
public:
    virtual ~MessageHandler() = default;

    virtual void onHello(const HelloRequest& hello) = 0;
    virtual void onPropertyUpdate(uint8_t propertyId, const void* value, size_t size) = 0;
    virtual void onError(const ErrorMessage& error) {}
    virtual void onPing(uint32_t payload) {}
};

/**
 * MessageRouter - Dispatch incoming binary messages to handlers
 */
class MessageRouter {
public:
    MessageRouter(MessageHandler* handler) : _handler(handler) {}

    bool process(const uint8_t* data, size_t length);

private:
    MessageHandler* _handler;

    bool processHello(ReadBuffer& buf);
    bool processPropertyUpdateShort(ReadBuffer& buf, bool batched);
    bool processPropertyUpdateLong(ReadBuffer& buf, bool batched);
    bool processError(ReadBuffer& buf);
    bool processPing(ReadBuffer& buf);
    PropertyBase* findProperty(uint8_t id);
};

} // namespace MicroProto

#endif // MICROPROTO_MESSAGES_MESSAGE_ROUTER_H