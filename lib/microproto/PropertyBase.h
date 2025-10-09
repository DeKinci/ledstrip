#ifndef MICROPROTO_PROPERTY_BASE_H
#define MICROPROTO_PROPERTY_BASE_H

#include <stdint.h>
#include <stddef.h>

namespace MicroProto {

enum class PropertyLevel : uint8_t {
    LOCAL = 0,
    GROUP = 1,
    GLOBAL = 2
};

class PropertyBase {
public:
    const uint8_t id;
    const char* name;
    const PropertyLevel level;
    const bool persistent;
    const bool readonly;
    const bool hidden;
    const bool ble_exposed;
    const uint8_t group_id;

    // Linked list for registration
    PropertyBase* next;
    static PropertyBase* head;

    PropertyBase(
        const char* name,
        PropertyLevel level,
        bool persistent = false,
        bool readonly = false,
        bool hidden = false,
        bool ble_exposed = false,
        uint8_t group_id = 0
    );

    // Virtual interface for type-erased operations
    virtual uint8_t getTypeId() const = 0;
    virtual size_t getSize() const = 0;
    virtual const void* getData() const = 0;
    virtual void setData(const void* data, size_t size) = 0;

    // Change callback - called immediately on value change
    using ChangeCallback = void (*)(uint8_t property_id);
    void setChangeCallback(ChangeCallback callback) { changeCallback = callback; }

protected:
    void notifyChange();

private:
    static uint8_t nextId;
    ChangeCallback changeCallback = nullptr;
};

} // namespace MicroProto

#endif // MICROPROTO_PROPERTY_BASE_H
