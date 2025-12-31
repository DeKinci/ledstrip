#ifndef MICROPROTO_RESOURCE_PROPERTY_H
#define MICROPROTO_RESOURCE_PROPERTY_H

#include "PropertyBase.h"
#ifdef ARDUINO
#include "PropertyStorage.h"
#endif
#include "ResourceStorage.h"
#include "TypeTraits.h"
#include "wire/Buffer.h"
#include "wire/TypeCodec.h"
#include <array>
#include <string.h>

namespace MicroProto {

/**
 * ResourceHeader - Common header fields for all resources
 *
 * Implicit fields present in every resource header:
 * - id: varint (server-assigned, unique within property)
 * - version: varint (increments on body update)
 * - size: varint (body size in bytes)
 */
struct ResourceHeader {
    uint32_t id = 0;
    uint32_t version = 0;
    uint32_t bodySize = 0;
    bool valid = false;

    void reset() {
        id = 0;
        version = 0;
        bodySize = 0;
        valid = false;
    }
};

/**
 * ResourceTypeDef - Describes header or body type structure
 */
struct ResourceTypeDef {
    uint8_t typeId;
    uint16_t size;

    ResourceTypeDef() : typeId(0), size(0) {}
    ResourceTypeDef(uint8_t type, uint16_t sz) : typeId(type), size(sz) {}
};

/**
 * ResourceProperty - Header/body split property for large data
 *
 * RESOURCE (0x24) stores multiple resources with automatic header sync
 * and on-demand body fetching. Read-only via PROPERTY_UPDATE.
 *
 * Storage:
 * - Headers → Memory (persisted to NVS if persistent=true)
 * - Bodies → SPIFFS at /res/{propName}/{resourceId}.bin
 *
 * Template parameters:
 *   MaxResources   - Maximum number of resources
 *   HeaderDataSize - Size of custom header data per resource
 */
template<size_t MaxResources, size_t HeaderDataSize>
class ResourceProperty : public PropertyBase {
public:
    static_assert(MaxResources > 0, "ResourceProperty must support at least 1 resource");
    static_assert(HeaderDataSize > 0, "ResourceProperty must have non-zero header size");

    ResourceProperty(
        const char* name,
        const ResourceTypeDef& headerType,
        const ResourceTypeDef& bodyType,
        PropertyLevel level = PropertyLevel::LOCAL,
        const char* description = nullptr,
        UIHints uiHints = UIHints(),
        bool persistent = false,
        bool hidden = false,
        bool ble_exposed = false,
        uint8_t group_id = 0
    ) : PropertyBase(name, level, persistent, true /* readonly */, hidden, ble_exposed, group_id, description, uiHints),
        _headerType(headerType),
        _bodyType(bodyType),
        _count(0),
        _nextId(1)
    {
        for (size_t i = 0; i < MaxResources; i++) {
            _headers[i].reset();
            _headerData[i].fill(0);
        }
    }

    // =========== Resource management ===========

    size_t resourceCount() const { return _count; }
    constexpr size_t maxResources() const { return MaxResources; }

    /**
     * Create a new resource (body saved to SPIFFS)
     * Returns resource ID (>0) on success, 0 on failure
     */
    uint32_t createResource(const void* headerData, size_t headerLen,
                            const void* bodyData, size_t bodyLen) {
        size_t slot = findEmptySlot();
        if (slot == MaxResources) return 0;

        if (headerLen > HeaderDataSize) headerLen = HeaderDataSize;

        uint32_t newId = _nextId++;

        // Save body to SPIFFS first
        if (bodyData && bodyLen > 0) {
            if (!ResourceStorage::writeBody(name, newId, bodyData, bodyLen)) {
                return 0;
            }
        }

        _headers[slot].id = newId;
        _headers[slot].version = 1;
        _headers[slot].bodySize = bodyLen;
        _headers[slot].valid = true;

        memcpy(_headerData[slot].data(), headerData, headerLen);

        _count++;
        notifyChange();
        return newId;
    }

    bool updateHeader(uint32_t resourceId, const void* headerData, size_t headerLen) {
        size_t slot = findSlotById(resourceId);
        if (slot == MaxResources) return false;

        if (headerLen > HeaderDataSize) headerLen = HeaderDataSize;
        memcpy(_headerData[slot].data(), headerData, headerLen);
        notifyChange();
        return true;
    }

    bool updateBody(uint32_t resourceId, const void* bodyData, size_t bodyLen) {
        size_t slot = findSlotById(resourceId);
        if (slot == MaxResources) return false;

        if (!ResourceStorage::writeBody(name, resourceId, bodyData, bodyLen)) {
            return false;
        }

        _headers[slot].bodySize = bodyLen;
        _headers[slot].version++;
        notifyChange();
        return true;
    }

    bool deleteResource(uint32_t resourceId) {
        size_t slot = findSlotById(resourceId);
        if (slot == MaxResources) return false;

        ResourceStorage::deleteBody(name, resourceId);

        _headers[slot].reset();
        _headerData[slot].fill(0);
        _count--;
        notifyChange();
        return true;
    }

    // =========== Resource access ===========

    const ResourceHeader* getHeader(uint32_t resourceId) const {
        size_t slot = findSlotById(resourceId);
        if (slot == MaxResources) return nullptr;
        return &_headers[slot];
    }

    const void* getHeaderData(uint32_t resourceId) const {
        size_t slot = findSlotById(resourceId);
        if (slot == MaxResources) return nullptr;
        return _headerData[slot].data();
    }

    /**
     * Read body from SPIFFS into buffer
     * Returns bytes read, or 0 on failure
     */
    size_t readBody(uint32_t resourceId, void* buffer, size_t bufferSize) const {
        size_t slot = findSlotById(resourceId);
        if (slot == MaxResources) return 0;
        return ResourceStorage::readBody(name, resourceId, buffer, bufferSize);
    }

    size_t getBodySize(uint32_t resourceId) const {
        const ResourceHeader* hdr = getHeader(resourceId);
        return hdr ? hdr->bodySize : 0;
    }

    template<typename Callback>
    void forEach(Callback callback) const {
        for (size_t i = 0; i < MaxResources; i++) {
            if (_headers[i].valid) {
                if (!callback(_headers[i].id, _headers[i], _headerData[i].data())) {
                    break;
                }
            }
        }
    }

    const ResourceHeader* getHeaderByIndex(size_t index) const {
        if (index >= MaxResources || !_headers[index].valid) return nullptr;
        return &_headers[index];
    }

    const void* getHeaderDataByIndex(size_t index) const {
        if (index >= MaxResources || !_headers[index].valid) return nullptr;
        return _headerData[index].data();
    }

    // =========== Type definitions ===========

    const ResourceTypeDef& headerTypeDef() const { return _headerType; }
    const ResourceTypeDef& bodyTypeDef() const { return _bodyType; }

    // =========== PropertyBase interface ===========

    uint8_t getTypeId() const override { return TYPE_RESOURCE; }

    size_t getSize() const override {
        return 2 + _count * (4 + 4 + 4 + HeaderDataSize);
    }

    const void* getData() const override {
        if (_count == 0) return nullptr;
        for (size_t i = 0; i < MaxResources; i++) {
            if (_headers[i].valid) return _headerData[i].data();
        }
        return nullptr;
    }

    void setData(const void* data, size_t size) override {
        // Read-only via PROPERTY_UPDATE
    }

    size_t getElementCount() const override { return _count; }
    bool validateValue(const void* data, size_t size) const override { return true; }
    size_t getHeaderDataSize() const { return HeaderDataSize; }

    // Schema encoding for RESOURCE type
    // Note: For basic types only - use TypedResourceProperty for complex types
    bool encodeTypeDefinition(WriteBuffer& buf) const override {
        // RESOURCE: type_id(0x24) + header_type_def + body_type_def
        if (!buf.writeByte(TYPE_RESOURCE)) return false;

        // Header type definition (basic type + validation_flags)
        if (!buf.writeByte(_headerType.typeId)) return false;
        if (!buf.writeByte(0)) return false;  // validation_flags = 0 (no constraints)

        // Body type definition (basic type + validation_flags)
        if (!buf.writeByte(_bodyType.typeId)) return false;
        if (!buf.writeByte(0)) return false;  // validation_flags = 0 (no constraints)

        return true;
    }

    // =========== NVS Persistence ===========

    bool saveToNVS() override {
        // Serialize: count(2) + nextId(4) + for each valid: slot(1) + id(4) + version(4) + bodySize(4) + headerData
        // Max size = 6 + MaxResources * (1 + 4 + 4 + 4 + HeaderDataSize)
        constexpr size_t maxSize = 6 + MaxResources * (13 + HeaderDataSize);
        uint8_t buffer[maxSize];
        size_t pos = 0;

        // Write count
        buffer[pos++] = static_cast<uint8_t>(_count & 0xFF);
        buffer[pos++] = static_cast<uint8_t>((_count >> 8) & 0xFF);

        // Write nextId
        memcpy(&buffer[pos], &_nextId, sizeof(uint32_t));
        pos += sizeof(uint32_t);

        // Write each valid resource header
        for (size_t i = 0; i < MaxResources; i++) {
            if (_headers[i].valid) {
                buffer[pos++] = static_cast<uint8_t>(i);  // slot index
                memcpy(&buffer[pos], &_headers[i].id, sizeof(uint32_t));
                pos += sizeof(uint32_t);
                memcpy(&buffer[pos], &_headers[i].version, sizeof(uint32_t));
                pos += sizeof(uint32_t);
                memcpy(&buffer[pos], &_headers[i].bodySize, sizeof(uint32_t));
                pos += sizeof(uint32_t);
                memcpy(&buffer[pos], _headerData[i].data(), HeaderDataSize);
                pos += HeaderDataSize;
            }
        }

#ifdef ARDUINO
        return PropertyStorage::saveRaw(id, buffer, pos);
#else
        (void)buffer; (void)pos;
        return true;  // Stub for native tests
#endif
    }

    bool loadFromNVS() override {
        constexpr size_t maxSize = 6 + MaxResources * (13 + HeaderDataSize);
        uint8_t buffer[maxSize];

#ifdef ARDUINO
        size_t bytesRead = PropertyStorage::loadRaw(id, buffer, maxSize);
#else
        size_t bytesRead = 0;  // Stub for native tests
#endif
        if (bytesRead < 6) return false;  // Minimum: count + nextId

        size_t pos = 0;

        // Read count
        uint16_t count = buffer[pos] | (buffer[pos + 1] << 8);
        pos += 2;

        // Read nextId
        memcpy(&_nextId, &buffer[pos], sizeof(uint32_t));
        pos += sizeof(uint32_t);

        // Clear current headers
        for (size_t i = 0; i < MaxResources; i++) {
            _headers[i].reset();
            _headerData[i].fill(0);
        }
        _count = 0;

        // Read each resource header
        for (uint16_t r = 0; r < count && pos < bytesRead; r++) {
            uint8_t slot = buffer[pos++];
            if (slot >= MaxResources) continue;

            memcpy(&_headers[slot].id, &buffer[pos], sizeof(uint32_t));
            pos += sizeof(uint32_t);
            memcpy(&_headers[slot].version, &buffer[pos], sizeof(uint32_t));
            pos += sizeof(uint32_t);
            memcpy(&_headers[slot].bodySize, &buffer[pos], sizeof(uint32_t));
            pos += sizeof(uint32_t);
            memcpy(_headerData[slot].data(), &buffer[pos], HeaderDataSize);
            pos += HeaderDataSize;

            _headers[slot].valid = true;
            _count++;
        }

        return true;
    }

private:
    ResourceTypeDef _headerType;
    ResourceTypeDef _bodyType;
    std::array<ResourceHeader, MaxResources> _headers;
    std::array<std::array<uint8_t, HeaderDataSize>, MaxResources> _headerData;
    size_t _count;
    uint32_t _nextId;

    size_t findEmptySlot() const {
        for (size_t i = 0; i < MaxResources; i++) {
            if (!_headers[i].valid) return i;
        }
        return MaxResources;
    }

    size_t findSlotById(uint32_t resourceId) const {
        for (size_t i = 0; i < MaxResources; i++) {
            if (_headers[i].valid && _headers[i].id == resourceId) return i;
        }
        return MaxResources;
    }
};

/**
 * TypedResourceProperty - Type-safe resource property with proper schema encoding
 *
 * Uses C++ types for header and body, enabling proper DATA_TYPE_DEFINITION
 * encoding in the schema. Supports any MicroProto type including OBJECT,
 * ARRAY, LIST, and nested containers.
 *
 * Template parameters:
 *   HeaderT      - C++ type for header data (must be wire-safe)
 *   BodyT        - C++ type for body data (must be wire-safe)
 *   MaxResources - Maximum number of resources
 *
 * Usage:
 *   struct ShaderHeader {
 *       std::array<uint8_t, 32> name;
 *       bool enabled;
 *   };
 *   MICROPROTO_FIELD_NAMES(ShaderHeader, "name", "enabled")
 *
 *   struct ShaderBody {
 *       std::array<uint8_t, 256> code;
 *       float speed;
 *   };
 *   MICROPROTO_FIELD_NAMES(ShaderBody, "code", "speed")
 *
 *   TypedResourceProperty<ShaderHeader, ShaderBody, 10> shaders("shaders");
 */
template<typename HeaderT, typename BodyT, size_t MaxResources>
class TypedResourceProperty : public PropertyBase {
public:
    static_assert(MaxResources > 0, "TypedResourceProperty must support at least 1 resource");
    static_assert(std::is_trivially_copyable_v<HeaderT>, "Header type must be trivially copyable");
    static_assert(std::is_trivially_copyable_v<BodyT>, "Body type must be trivially copyable");

    static constexpr size_t HeaderDataSize = sizeof(HeaderT);
    static constexpr size_t BodyDataSize = sizeof(BodyT);

    TypedResourceProperty(
        const char* name,
        PropertyLevel level = PropertyLevel::LOCAL,
        const char* description = nullptr,
        UIHints uiHints = UIHints(),
        bool persistent = false,
        bool hidden = false,
        bool ble_exposed = false,
        uint8_t group_id = 0
    ) : PropertyBase(name, level, persistent, true /* readonly */, hidden, ble_exposed, group_id, description, uiHints),
        _count(0),
        _nextId(1)
    {
        for (size_t i = 0; i < MaxResources; i++) {
            _headers[i].reset();
            memset(&_headerData[i], 0, sizeof(HeaderT));
        }
    }

    // =========== Resource management ===========

    size_t resourceCount() const { return _count; }
    constexpr size_t maxResources() const { return MaxResources; }

    /**
     * Create a new resource
     * Returns resource ID (>0) on success, 0 on failure
     */
    uint32_t createResource(const HeaderT& header, const BodyT& body) {
        size_t slot = findEmptySlot();
        if (slot == MaxResources) return 0;

        uint32_t newId = _nextId++;

        // Save body to SPIFFS
        if (!ResourceStorage::writeBody(name, newId, &body, sizeof(BodyT))) {
            return 0;
        }

        _headers[slot].id = newId;
        _headers[slot].version = 1;
        _headers[slot].bodySize = sizeof(BodyT);
        _headers[slot].valid = true;
        _headerData[slot] = header;

        _count++;
        notifyChange();
        return newId;
    }

    bool updateHeader(uint32_t resourceId, const HeaderT& header) {
        size_t slot = findSlotById(resourceId);
        if (slot == MaxResources) return false;

        _headerData[slot] = header;
        notifyChange();
        return true;
    }

    bool updateBody(uint32_t resourceId, const BodyT& body) {
        size_t slot = findSlotById(resourceId);
        if (slot == MaxResources) return false;

        if (!ResourceStorage::writeBody(name, resourceId, &body, sizeof(BodyT))) {
            return false;
        }

        _headers[slot].bodySize = sizeof(BodyT);
        _headers[slot].version++;
        notifyChange();
        return true;
    }

    bool deleteResource(uint32_t resourceId) {
        size_t slot = findSlotById(resourceId);
        if (slot == MaxResources) return false;

        ResourceStorage::deleteBody(name, resourceId);

        _headers[slot].reset();
        memset(&_headerData[slot], 0, sizeof(HeaderT));
        _count--;
        notifyChange();
        return true;
    }

    // =========== Resource access ===========

    const ResourceHeader* getHeader(uint32_t resourceId) const {
        size_t slot = findSlotById(resourceId);
        if (slot == MaxResources) return nullptr;
        return &_headers[slot];
    }

    const HeaderT* getHeaderData(uint32_t resourceId) const {
        size_t slot = findSlotById(resourceId);
        if (slot == MaxResources) return nullptr;
        return &_headerData[slot];
    }

    /**
     * Read body from SPIFFS
     * Returns true on success
     */
    bool readBody(uint32_t resourceId, BodyT& body) const {
        size_t slot = findSlotById(resourceId);
        if (slot == MaxResources) return false;
        return ResourceStorage::readBody(name, resourceId, &body, sizeof(BodyT)) == sizeof(BodyT);
    }

    template<typename Callback>
    void forEach(Callback callback) const {
        for (size_t i = 0; i < MaxResources; i++) {
            if (_headers[i].valid) {
                if (!callback(_headers[i].id, _headers[i], _headerData[i])) {
                    break;
                }
            }
        }
    }

    // =========== PropertyBase interface ===========

    uint8_t getTypeId() const override { return TYPE_RESOURCE; }

    size_t getSize() const override {
        return 2 + _count * (4 + 4 + 4 + sizeof(HeaderT));
    }

    const void* getData() const override {
        if (_count == 0) return nullptr;
        for (size_t i = 0; i < MaxResources; i++) {
            if (_headers[i].valid) return &_headerData[i];
        }
        return nullptr;
    }

    void setData(const void* data, size_t size) override {
        // Read-only via PROPERTY_UPDATE
    }

    size_t getElementCount() const override { return _count; }
    bool validateValue(const void* data, size_t size) const override { return true; }

    // Schema encoding with proper DATA_TYPE_DEFINITION for header and body types
    bool encodeTypeDefinition(WriteBuffer& buf) const override {
        // RESOURCE: type_id(0x24) + header_data_type_def + body_data_type_def
        if (!buf.writeByte(TYPE_RESOURCE)) return false;

        // Encode header type using SchemaTypeEncoder
        if (!SchemaTypeEncoder::encodeElement<HeaderT>(buf, nullptr)) return false;

        // Encode body type using SchemaTypeEncoder
        if (!SchemaTypeEncoder::encodeElement<BodyT>(buf, nullptr)) return false;

        return true;
    }

    // =========== NVS Persistence ===========

    bool saveToNVS() override {
        // Serialize: count(2) + nextId(4) + for each valid: slot(1) + id(4) + version(4) + bodySize(4) + headerData
        constexpr size_t maxSize = 6 + MaxResources * (13 + sizeof(HeaderT));
        uint8_t buffer[maxSize];
        size_t pos = 0;

        // Write count
        buffer[pos++] = static_cast<uint8_t>(_count & 0xFF);
        buffer[pos++] = static_cast<uint8_t>((_count >> 8) & 0xFF);

        // Write nextId
        memcpy(&buffer[pos], &_nextId, sizeof(uint32_t));
        pos += sizeof(uint32_t);

        // Write each valid resource header
        for (size_t i = 0; i < MaxResources; i++) {
            if (_headers[i].valid) {
                buffer[pos++] = static_cast<uint8_t>(i);  // slot index
                memcpy(&buffer[pos], &_headers[i].id, sizeof(uint32_t));
                pos += sizeof(uint32_t);
                memcpy(&buffer[pos], &_headers[i].version, sizeof(uint32_t));
                pos += sizeof(uint32_t);
                memcpy(&buffer[pos], &_headers[i].bodySize, sizeof(uint32_t));
                pos += sizeof(uint32_t);
                memcpy(&buffer[pos], &_headerData[i], sizeof(HeaderT));
                pos += sizeof(HeaderT);
            }
        }

#ifdef ARDUINO
        return PropertyStorage::saveRaw(id, buffer, pos);
#else
        (void)buffer; (void)pos;
        return true;  // Stub for native tests
#endif
    }

    bool loadFromNVS() override {
        constexpr size_t maxSize = 6 + MaxResources * (13 + sizeof(HeaderT));
        uint8_t buffer[maxSize];

#ifdef ARDUINO
        size_t bytesRead = PropertyStorage::loadRaw(id, buffer, maxSize);
#else
        size_t bytesRead = 0;  // Stub for native tests
#endif
        if (bytesRead < 6) return false;  // Minimum: count + nextId

        size_t pos = 0;

        // Read count
        uint16_t count = buffer[pos] | (buffer[pos + 1] << 8);
        pos += 2;

        // Read nextId
        memcpy(&_nextId, &buffer[pos], sizeof(uint32_t));
        pos += sizeof(uint32_t);

        // Clear current headers
        for (size_t i = 0; i < MaxResources; i++) {
            _headers[i].reset();
            memset(&_headerData[i], 0, sizeof(HeaderT));
        }
        _count = 0;

        // Read each resource header
        for (uint16_t r = 0; r < count && pos < bytesRead; r++) {
            uint8_t slot = buffer[pos++];
            if (slot >= MaxResources) continue;

            memcpy(&_headers[slot].id, &buffer[pos], sizeof(uint32_t));
            pos += sizeof(uint32_t);
            memcpy(&_headers[slot].version, &buffer[pos], sizeof(uint32_t));
            pos += sizeof(uint32_t);
            memcpy(&_headers[slot].bodySize, &buffer[pos], sizeof(uint32_t));
            pos += sizeof(uint32_t);
            memcpy(&_headerData[slot], &buffer[pos], sizeof(HeaderT));
            pos += sizeof(HeaderT);

            _headers[slot].valid = true;
            _count++;
        }

        return true;
    }

private:
    std::array<ResourceHeader, MaxResources> _headers;
    std::array<HeaderT, MaxResources> _headerData;
    size_t _count;
    uint32_t _nextId;

    size_t findEmptySlot() const {
        for (size_t i = 0; i < MaxResources; i++) {
            if (!_headers[i].valid) return i;
        }
        return MaxResources;
    }

    size_t findSlotById(uint32_t resourceId) const {
        for (size_t i = 0; i < MaxResources; i++) {
            if (_headers[i].valid && _headers[i].id == resourceId) return i;
        }
        return MaxResources;
    }
};

} // namespace MicroProto

#endif // MICROPROTO_RESOURCE_PROPERTY_H