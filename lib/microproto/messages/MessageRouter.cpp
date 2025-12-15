#include "MessageRouter.h"
#include <Arduino.h>

namespace MicroProto {

bool MessageRouter::process(const uint8_t* data, size_t length) {
    if (length == 0) return false;

    ReadBuffer buf(data, length);

    // Peek at opcode
    uint8_t headerByte = buf.peekByte();
    OpHeader header = OpHeader::decode(headerByte);

    switch (header.getOpCode()) {
        case OpCode::HELLO:
            return processHello(buf);

        case OpCode::PROPERTY_UPDATE_SHORT:
            return processPropertyUpdateShort(buf, header.batch);

        case OpCode::PROPERTY_UPDATE_LONG:
            return processPropertyUpdateLong(buf, header.batch);

        case OpCode::ERROR:
            return processError(buf);

        case OpCode::PING:
            return processPing(buf);

        default:
            return false;
    }
}

bool MessageRouter::processHello(ReadBuffer& buf) {
    HelloRequest hello;
    if (!HelloRequest::decode(buf, hello)) {
        return false;
    }
    _handler->onHello(hello);
    return true;
}

bool MessageRouter::processPropertyUpdateShort(ReadBuffer& buf, bool batched) {
    buf.readByte();  // Skip header

    uint8_t count = 1;
    if (batched) {
        count = buf.readByte() + 1;
    }

    size_t minBytesNeeded = count * 3;
    if (buf.remaining() < minBytesNeeded) {
        Serial.printf("[MicroProto] Buffer underflow: need %d bytes for %d items, have %d\n",
                      minBytesNeeded, count, buf.remaining());
        return false;
    }

    for (uint8_t i = 0; i < count && buf.ok(); i++) {
        uint8_t propId = buf.readByte();
        PropertyUpdateFlags flags = PropertyUpdateFlags::decode(buf.readByte());

        if (flags.has_timestamp) buf.skip(4);
        if (flags.has_version) buf.skip(8);

        if (!buf.ok()) {
            Serial.printf("[MicroProto] Buffer underflow reading property %d flags\n", propId);
            return false;
        }

        PropertyBase* prop = findProperty(propId);
        if (!prop) {
            Serial.printf("[MicroProto] Unknown property ID %d in update\n", propId);
            return false;
        }

        union {
            bool b;
            int8_t i8;
            uint8_t u8;
            int32_t i32;
            float f32;
        } value;

        size_t size = prop->getSize();
        if (!TypeCodec::decode(buf, prop->getTypeId(), &value, size)) {
            Serial.printf("[MicroProto] Failed to decode value for property %d\n", propId);
            return false;
        }

        _handler->onPropertyUpdate(propId, &value, size);
    }

    return buf.ok();
}

bool MessageRouter::processPropertyUpdateLong(ReadBuffer& buf, bool batched) {
    buf.readByte();  // Skip header

    uint8_t count = 1;
    if (batched) {
        count = buf.readByte() + 1;
    }

    size_t minBytesNeeded = count * 4;
    if (buf.remaining() < minBytesNeeded) {
        Serial.printf("[MicroProto] Buffer underflow (long): need %d bytes for %d items, have %d\n",
                      minBytesNeeded, count, buf.remaining());
        return false;
    }

    for (uint8_t i = 0; i < count && buf.ok(); i++) {
        uint16_t propId = buf.readUint16();
        PropertyUpdateFlags flags = PropertyUpdateFlags::decode(buf.readByte());

        if (flags.has_timestamp) buf.skip(4);
        if (flags.has_version) buf.skip(8);

        if (!buf.ok()) {
            Serial.printf("[MicroProto] Buffer underflow reading property %d (long)\n", propId);
            return false;
        }

        if (propId > 255) {
            Serial.printf("[MicroProto] Property ID %d exceeds u8 range\n", propId);
            return false;
        }

        PropertyBase* prop = findProperty(static_cast<uint8_t>(propId));
        if (!prop) {
            Serial.printf("[MicroProto] Unknown property ID %d in update (long)\n", propId);
            return false;
        }

        union {
            bool b;
            int8_t i8;
            uint8_t u8;
            int32_t i32;
            float f32;
        } value;

        size_t size = prop->getSize();
        if (!TypeCodec::decode(buf, prop->getTypeId(), &value, size)) {
            Serial.printf("[MicroProto] Failed to decode value for property %d (long)\n", propId);
            return false;
        }

        _handler->onPropertyUpdate(static_cast<uint8_t>(propId), &value, size);
    }

    return buf.ok();
}

bool MessageRouter::processError(ReadBuffer& buf) {
    ErrorMessage error;
    if (!ErrorMessage::decode(buf, error)) {
        return false;
    }
    _handler->onError(error);
    return true;
}

bool MessageRouter::processPing(ReadBuffer& buf) {
    buf.readByte();  // Skip header

    uint32_t payload = 0;
    if (buf.remaining() >= 4) {
        payload = buf.readUint32();
    }

    Serial.printf("[MicroProto] PING received, payload=%lu\n", payload);
    _handler->onPing(payload);
    return true;
}

PropertyBase* MessageRouter::findProperty(uint8_t id) {
    for (PropertyBase* p = PropertyBase::head; p; p = p->next) {
        if (p->id == id) return p;
    }
    return nullptr;
}

} // namespace MicroProto