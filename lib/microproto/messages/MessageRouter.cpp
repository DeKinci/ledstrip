#include "MessageRouter.h"
#include <Arduino.h>

namespace MicroProto {

bool MessageRouter::process(const uint8_t* data, size_t length) {
    if (length == 0) return false;

    ReadBuffer buf(data, length);

    // Read and decode operation header
    uint8_t headerByte = buf.readByte();
    if (!buf.ok()) return false;

    OpCode opcode;
    uint8_t flags;
    decodeOpHeader(headerByte, opcode, flags);

    switch (opcode) {
        case OpCode::HELLO:
            return processHello(buf, flags);

        case OpCode::PROPERTY_UPDATE:
            return processPropertyUpdate(buf, flags);

        case OpCode::ERROR:
            return processError(buf, flags);

        case OpCode::PING:
            return processPing(buf, flags);

        case OpCode::RPC:
            return processRpc(buf, flags);

        case OpCode::RESOURCE_GET:
            return processResourceGet(buf, flags);

        case OpCode::RESOURCE_PUT:
            return processResourcePut(buf, flags);

        case OpCode::RESOURCE_DELETE:
            return processResourceDelete(buf, flags);

        default:
            Serial.printf("[MicroProto] Unknown opcode: 0x%02X\n", static_cast<uint8_t>(opcode));
            return false;
    }
}

bool MessageRouter::processHello(ReadBuffer& buf, uint8_t flags) {
    Hello hello;
    if (!Hello::decodePayload(buf, (flags & Flags::IS_RESPONSE) != 0, hello)) {
        return false;
    }
    _handler->onHello(hello);
    return true;
}

bool MessageRouter::processPropertyUpdate(ReadBuffer& buf, uint8_t flags) {
    PropertyUpdateFlags updateFlags = PropertyUpdateFlags::decode(flags);

    // Read batch count if batched
    uint8_t count = 1;
    if (updateFlags.batch) {
        count = buf.readByte() + 1;
        if (!buf.ok()) return false;
    }

    // Read timestamp if present (once for entire batch)
    uint32_t timestamp = 0;
    if (updateFlags.hasTimestamp) {
        timestamp = buf.readVarint();
        if (!buf.ok()) return false;
    }

    // Process each property update
    for (uint8_t i = 0; i < count && buf.ok(); i++) {
        // Read property ID (propid encoding: 1-2 bytes)
        uint16_t propId = buf.readPropId();
        if (!buf.ok()) {
            Serial.printf("[MicroProto] Buffer underflow reading property ID\n");
            return false;
        }

        // For MVP: all properties are LOCAL (no version fields)
        // TODO: Check property level and read version/source_node_id for GROUP/GLOBAL

        PropertyBase* prop = findProperty(propId);
        if (!prop) {
            Serial.printf("[MicroProto] Unknown property ID %d in update\n", propId);
            return false;
        }

        // Decode directly into property (handles basic + container types)
        if (!TypeCodec::decodeProperty(buf, prop)) {
            Serial.printf("[MicroProto] Failed to decode value for property %d\n", propId);
            return false;
        }

        // Notify handler
        _handler->onPropertyUpdate(propId, prop->getData(), prop->getSize());
    }

    return buf.ok();
}

bool MessageRouter::processError(ReadBuffer& buf, uint8_t flags) {
    ErrorMessage error;
    if (!ErrorMessage::decodePayload(buf, flags, error)) {
        return false;
    }
    _handler->onError(error);
    return true;
}

bool MessageRouter::processPing(ReadBuffer& buf, uint8_t flags) {
    bool isResponse = (flags & Flags::IS_RESPONSE) != 0;

    // Read varint payload
    uint32_t payload = buf.readVarint();
    if (!buf.ok()) {
        payload = 0;  // Empty ping is ok
    }

    _handler->onPing(isResponse, payload);
    return true;
}

bool MessageRouter::processRpc(ReadBuffer& buf, uint8_t flags) {
    RpcFlags rpcFlags = RpcFlags::decode(flags);

    // Read function ID (propid encoding)
    uint16_t functionId = buf.readPropId();
    if (!buf.ok()) return false;

    if (rpcFlags.isResponse) {
        // RPC Response
        uint8_t callId = buf.readByte();
        if (!buf.ok()) return false;

        if (rpcFlags.success) {
            // Success response
            _handler->onRpcResponse(callId, true, buf);
        } else {
            // Error response
            uint8_t errorCode = buf.readByte();
            size_t msgLen;
            const char* errorMsg = buf.readUtf8(msgLen);
            _handler->onRpcError(callId, errorCode, errorMsg, msgLen);
        }
    } else {
        // RPC Request
        uint8_t callId = 0;
        if (rpcFlags.needsResponse) {
            callId = buf.readByte();
            if (!buf.ok()) return false;
        }

        _handler->onRpcRequest(functionId, callId, rpcFlags.needsResponse, buf);
    }

    return true;
}

bool MessageRouter::processResourceGet(ReadBuffer& buf, uint8_t flags) {
    bool isResponse = flags & Flags::IS_RESPONSE;

    uint8_t requestId = buf.readByte();
    if (!buf.ok()) return false;

    if (isResponse) {
        // Response
        bool statusError = flags & Flags::STATUS_ERROR;
        if (!statusError) {
            // OK response: data follows as blob
            size_t dataLen;
            const uint8_t* data = buf.readBlob(dataLen);
            _handler->onResourceGetResponse(requestId, true, data, dataLen);
        } else {
            // Error response (no data)
            _handler->onResourceGetResponse(requestId, false, nullptr, 0);
        }
    } else {
        // Request
        uint16_t propertyId = buf.readPropId();
        if (!buf.ok()) return false;

        uint32_t resourceId = buf.readVarint();
        if (!buf.ok()) return false;

        _handler->onResourceGetRequest(requestId, propertyId, resourceId);
    }

    return true;
}

bool MessageRouter::processResourcePut(ReadBuffer& buf, uint8_t flags) {
    ResourcePutFlags putFlags = ResourcePutFlags::decode(flags);

    uint8_t requestId = buf.readByte();
    if (!buf.ok()) return false;

    if (putFlags.isResponse) {
        // Response
        if (!putFlags.statusError) {
            // OK: resourceId follows
            uint32_t resourceId = buf.readVarint();
            if (!buf.ok()) return false;
            _handler->onResourcePutResponse(requestId, true, resourceId);
        } else {
            // Error
            _handler->onResourcePutResponse(requestId, false, 0);
        }
    } else {
        // Request
        uint16_t propertyId = buf.readPropId();
        if (!buf.ok()) return false;

        uint32_t resourceId = buf.readVarint();
        if (!buf.ok()) return false;

        const uint8_t* headerData = nullptr;
        size_t headerLen = 0;
        const uint8_t* bodyData = nullptr;
        size_t bodyLen = 0;

        if (putFlags.updateHeader) {
            headerData = buf.readBlob(headerLen);
            if (!buf.ok()) return false;
        }

        if (putFlags.updateBody) {
            bodyData = buf.readBlob(bodyLen);
            if (!buf.ok()) return false;
        }

        _handler->onResourcePutRequest(requestId, propertyId, resourceId,
                                        headerData, headerLen, bodyData, bodyLen);
    }

    return true;
}

bool MessageRouter::processResourceDelete(ReadBuffer& buf, uint8_t flags) {
    bool isResponse = flags & Flags::IS_RESPONSE;

    uint8_t requestId = buf.readByte();
    if (!buf.ok()) return false;

    if (isResponse) {
        // Response
        bool statusError = flags & Flags::STATUS_ERROR;
        _handler->onResourceDeleteResponse(requestId, !statusError);
    } else {
        // Request
        uint16_t propertyId = buf.readPropId();
        if (!buf.ok()) return false;

        uint32_t resourceId = buf.readVarint();
        if (!buf.ok()) return false;

        _handler->onResourceDeleteRequest(requestId, propertyId, resourceId);
    }

    return true;
}

PropertyBase* MessageRouter::findProperty(uint16_t id) {
    if (id > 255) return nullptr;  // MVP limit
    return PropertyBase::find(static_cast<uint8_t>(id));
}

} // namespace MicroProto