#ifndef MICROPROTO_MESSAGES_MESSAGE_ROUTER_H
#define MICROPROTO_MESSAGES_MESSAGE_ROUTER_H

#include "../wire/Buffer.h"
#include "../wire/OpCode.h"
#include "../wire/TypeCodec.h"
#include "../PropertyBase.h"
#include "Hello.h"
#include "Error.h"

namespace MicroProto {

/**
 * MessageHandler - Callback interface for message handling
 *
 * Implement this interface to handle incoming protocol messages.
 */
class MessageHandler {
public:
    virtual ~MessageHandler() = default;

    /**
     * Called when HELLO message received
     * @param hello Decoded Hello message (check isResponse to determine type)
     */
    virtual void onHello(const Hello& hello) = 0;

    /**
     * Called for each property update in PROPERTY_UPDATE message
     * @param propertyId Property ID (0-32767)
     * @param value Pointer to decoded value
     * @param size Size of value in bytes
     */
    virtual void onPropertyUpdate(uint16_t propertyId, const void* value, size_t size) = 0;

    /**
     * Called when ERROR message received
     * @param error Decoded error message
     */
    virtual void onError(const ErrorMessage& error) {}

    /**
     * Called when PING message received
     * @param isResponse true if this is a response, false if request
     * @param payload Echo payload value
     */
    virtual void onPing(bool isResponse, uint32_t payload) {}

    /**
     * Called when RPC request received
     * @param functionId Function to call (propid)
     * @param callId Call ID for response (0 if fire-and-forget)
     * @param needsResponse true if caller expects response
     * @param params ReadBuffer positioned at parameter data
     */
    virtual void onRpcRequest(uint16_t functionId, uint8_t callId,
                               bool needsResponse, ReadBuffer& params) {}

    /**
     * Called when RPC response received
     * @param callId Call ID matching original request
     * @param success true if call succeeded
     * @param result ReadBuffer positioned at return value (if success && has_return_value)
     */
    virtual void onRpcResponse(uint8_t callId, bool success, ReadBuffer& result) {}

    /**
     * Called when RPC error response received
     * @param callId Call ID matching original request
     * @param errorCode Application error code
     * @param message Error message (NOT null-terminated!)
     * @param messageLen Length of error message
     */
    virtual void onRpcError(uint8_t callId, uint8_t errorCode,
                            const char* message, size_t messageLen) {}

    /**
     * Called when property validation fails
     * @param propertyId Property that failed validation
     * @param code Error code
     */
    virtual void onConstraintViolation(uint16_t propertyId, ErrorCode code) {}

    // =========== Resource Operations ===========

    /**
     * Called when RESOURCE_GET request received
     * @param requestId Request ID for response
     * @param propertyId Resource property ID
     * @param resourceId Resource ID within the property
     */
    virtual void onResourceGetRequest(uint8_t requestId, uint16_t propertyId, uint32_t resourceId) {}

    /**
     * Called when RESOURCE_GET response received
     * @param requestId Matching request ID
     * @param success true if resource found
     * @param data Resource body data (if success)
     * @param dataLen Length of body data
     */
    virtual void onResourceGetResponse(uint8_t requestId, bool success,
                                        const uint8_t* data, size_t dataLen) {}

    /**
     * Called when RESOURCE_PUT request received
     * @param requestId Request ID for response
     * @param propertyId Resource property ID
     * @param resourceId Resource ID (0 = create new)
     * @param headerData Header data (nullptr if not updating)
     * @param headerLen Header data length
     * @param bodyData Body data (nullptr if not updating)
     * @param bodyLen Body data length
     */
    virtual void onResourcePutRequest(uint8_t requestId, uint16_t propertyId,
                                       uint32_t resourceId,
                                       const uint8_t* headerData, size_t headerLen,
                                       const uint8_t* bodyData, size_t bodyLen) {}

    /**
     * Called when RESOURCE_PUT response received
     * @param requestId Matching request ID
     * @param success true if operation succeeded
     * @param resourceId Assigned or confirmed resource ID
     */
    virtual void onResourcePutResponse(uint8_t requestId, bool success, uint32_t resourceId) {}

    /**
     * Called when RESOURCE_DELETE request received
     * @param requestId Request ID for response
     * @param propertyId Resource property ID
     * @param resourceId Resource ID to delete
     */
    virtual void onResourceDeleteRequest(uint8_t requestId, uint16_t propertyId, uint32_t resourceId) {}

    /**
     * Called when RESOURCE_DELETE response received
     * @param requestId Matching request ID
     * @param success true if deletion succeeded
     */
    virtual void onResourceDeleteResponse(uint8_t requestId, bool success) {}
};

/**
 * MessageRouter - Dispatch incoming binary messages to handlers
 *
 * Decodes the operation header and routes to appropriate handler method.
 */
class MessageRouter {
public:
    MessageRouter(MessageHandler* handler) : _handler(handler) {}

    /**
     * Process an incoming message
     * @param data Raw message bytes
     * @param length Number of bytes
     * @return true if message was valid and processed
     */
    bool process(const uint8_t* data, size_t length);

private:
    MessageHandler* _handler;

    bool processHello(ReadBuffer& buf, uint8_t flags);
    bool processPropertyUpdate(ReadBuffer& buf, uint8_t flags);
    bool processError(ReadBuffer& buf, uint8_t flags);
    bool processPing(ReadBuffer& buf, uint8_t flags);
    bool processRpc(ReadBuffer& buf, uint8_t flags);
    bool processResourceGet(ReadBuffer& buf, uint8_t flags);
    bool processResourcePut(ReadBuffer& buf, uint8_t flags);
    bool processResourceDelete(ReadBuffer& buf, uint8_t flags);
    PropertyBase* findProperty(uint16_t id);
};

} // namespace MicroProto

#endif // MICROPROTO_MESSAGES_MESSAGE_ROUTER_H