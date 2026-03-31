#ifndef MICROPROTO_WIRE_BUFFER_H
#define MICROPROTO_WIRE_BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace MicroProto {

/**
 * WriteBuffer - Writes data to a fixed-size byte buffer
 *
 * Zero-copy, no allocations. Caller owns the buffer.
 */
class WriteBuffer {
public:
    WriteBuffer(uint8_t* buffer, size_t capacity)
        : _buffer(buffer), _capacity(capacity), _pos(0), _overflow(false) {}

    // Reset to beginning
    void reset() {
        _pos = 0;
        _overflow = false;
    }

    // Current write position
    size_t position() const { return _pos; }

    // Set write position (for rollback on failed writes)
    void setPosition(size_t pos) {
        if (pos <= _capacity) {
            _pos = pos;
            _overflow = false;
        }
    }

    // Bytes remaining
    size_t remaining() const { return _capacity - _pos; }

    // Check if overflow occurred
    bool overflow() const { return _overflow; }

    // Check if write succeeded (no overflow)
    bool ok() const { return !_overflow; }

    // Get pointer to buffer start
    const uint8_t* data() const { return _buffer; }

    // Write single byte
    bool writeByte(uint8_t value) {
        if (_pos >= _capacity) {
            _overflow = true;
            return false;
        }
        _buffer[_pos++] = value;
        return true;
    }

    // Write multiple bytes
    bool writeBytes(const uint8_t* data, size_t len) {
        if (_pos + len > _capacity) {
            _overflow = true;
            return false;
        }
        memcpy(_buffer + _pos, data, len);
        _pos += len;
        return true;
    }

    // Write varint (variable-length integer)
    // Returns number of bytes written, 0 on overflow
    size_t writeVarint(uint32_t value) {
        size_t start = _pos;
        do {
            uint8_t byte = value & 0x7F;
            value >>= 7;
            if (value != 0) {
                byte |= 0x80;  // Set continuation bit
            }
            if (!writeByte(byte)) {
                return 0;
            }
        } while (value != 0);
        return _pos - start;
    }

    // Write uint8
    bool writeUint8(uint8_t value) {
        return writeByte(value);
    }

    // Write int8
    bool writeInt8(int8_t value) {
        return writeByte(static_cast<uint8_t>(value));
    }

    // Write uint16 (little-endian)
    bool writeUint16(uint16_t value) {
        if (_pos + 2 > _capacity) {
            _overflow = true;
            return false;
        }
        _buffer[_pos++] = value & 0xFF;
        _buffer[_pos++] = (value >> 8) & 0xFF;
        return true;
    }

    // Write int32 (little-endian)
    bool writeInt32(int32_t value) {
        return writeUint32(static_cast<uint32_t>(value));
    }

    // Write uint32 (little-endian)
    bool writeUint32(uint32_t value) {
        if (_pos + 4 > _capacity) {
            _overflow = true;
            return false;
        }
        _buffer[_pos++] = value & 0xFF;
        _buffer[_pos++] = (value >> 8) & 0xFF;
        _buffer[_pos++] = (value >> 16) & 0xFF;
        _buffer[_pos++] = (value >> 24) & 0xFF;
        return true;
    }

    // Write float32 (IEEE 754, little-endian)
    bool writeFloat32(float value) {
        uint32_t bits;
        memcpy(&bits, &value, sizeof(bits));
        return writeUint32(bits);
    }

    // Write bool (single byte)
    bool writeBool(bool value) {
        return writeByte(value ? 1 : 0);
    }

    /**
     * Write propid (1-2 byte property/function ID)
     *
     * Format:
     *   0-127:     1 byte  [0xxxxxxx]
     *   128-32767: 2 bytes [1xxxxxxx] [xxxxxxxx]
     *
     * Returns true on success, false on overflow or invalid ID
     */
    bool writePropId(uint16_t id) {
        if (id <= 127) {
            return writeByte(static_cast<uint8_t>(id));
        } else if (id <= 32767) {
            // Low 7 bits with high bit set, then high 8 bits
            return writeByte(0x80 | (id & 0x7F)) &&
                   writeByte(static_cast<uint8_t>(id >> 7));
        }
        // ID out of range (> 32767)
        _overflow = true;
        return false;
    }

    /**
     * Write ident (length-prefixed ASCII identifier)
     *
     * Format: u8 length + ASCII bytes
     * Valid chars: a-z, A-Z, 0-9, _
     * Max length: 255 bytes
     */
    bool writeIdent(const char* str) {
        if (!str) {
            return writeByte(0);
        }
        size_t len = 0;
        while (str[len] != '\0' && len < 256) len++;
        if (len > 255) {
            _overflow = true;
            return false;
        }
        return writeByte(static_cast<uint8_t>(len)) &&
               writeBytes(reinterpret_cast<const uint8_t*>(str), len);
    }

    /**
     * Write utf8 string (varint length + UTF-8 bytes)
     */
    bool writeUtf8(const char* str) {
        if (!str) {
            return writeVarint(0) > 0;
        }
        size_t len = 0;
        while (str[len] != '\0') len++;
        return writeVarint(static_cast<uint32_t>(len)) > 0 &&
               writeBytes(reinterpret_cast<const uint8_t*>(str), len);
    }

    /**
     * Write blob (varint length + raw bytes)
     */
    bool writeBlob(const uint8_t* data, size_t len) {
        return writeVarint(static_cast<uint32_t>(len)) > 0 &&
               writeBytes(data, len);
    }

private:
    uint8_t* _buffer;
    size_t _capacity;
    size_t _pos;
    bool _overflow;
};

/**
 * ReadBuffer - Reads data from a byte buffer
 *
 * Zero-copy, no allocations. Does not own the buffer.
 */
class ReadBuffer {
public:
    ReadBuffer(const uint8_t* buffer, size_t length)
        : _buffer(buffer), _length(length), _pos(0), _error(false) {}

    // Reset to beginning
    void reset() {
        _pos = 0;
        _error = false;
    }

    // Current read position
    size_t position() const { return _pos; }

    // Bytes remaining
    size_t remaining() const { return _length - _pos; }

    // Check if error occurred (read past end)
    bool error() const { return _error; }

    // Check if read succeeded (no error)
    bool ok() const { return !_error; }

    // Get pointer to buffer start
    const uint8_t* data() const { return _buffer; }

    // Read single byte
    uint8_t readByte() {
        if (_pos >= _length) {
            _error = true;
            return 0;
        }
        return _buffer[_pos++];
    }

    // Peek at next byte without consuming
    uint8_t peekByte() const {
        if (_pos >= _length) {
            return 0;
        }
        return _buffer[_pos];
    }

    // Read multiple bytes
    bool readBytes(uint8_t* dest, size_t len) {
        if (_pos + len > _length) {
            _error = true;
            return false;
        }
        memcpy(dest, _buffer + _pos, len);
        _pos += len;
        return true;
    }

    // Skip bytes
    bool skip(size_t len) {
        if (_pos + len > _length) {
            _error = true;
            return false;
        }
        _pos += len;
        return true;
    }

    // Read varint (variable-length integer)
    // Returns value, sets error flag on failure
    uint32_t readVarint() {
        uint32_t result = 0;
        uint8_t shift = 0;

        for (int i = 0; i < 5; i++) {  // Max 5 bytes for 32-bit
            if (_pos >= _length) {
                _error = true;
                return 0;
            }
            uint8_t byte = _buffer[_pos++];
            result |= static_cast<uint32_t>(byte & 0x7F) << shift;

            if ((byte & 0x80) == 0) {
                return result;  // No continuation bit
            }
            shift += 7;
        }

        // Too many bytes
        _error = true;
        return 0;
    }

    // Read uint8
    uint8_t readUint8() {
        return readByte();
    }

    // Read int8
    int8_t readInt8() {
        return static_cast<int8_t>(readByte());
    }

    // Read uint16 (little-endian)
    uint16_t readUint16() {
        if (_pos + 2 > _length) {
            _error = true;
            return 0;
        }
        uint16_t value = _buffer[_pos] | (static_cast<uint16_t>(_buffer[_pos + 1]) << 8);
        _pos += 2;
        return value;
    }

    // Read int32 (little-endian)
    int32_t readInt32() {
        return static_cast<int32_t>(readUint32());
    }

    // Read uint32 (little-endian)
    uint32_t readUint32() {
        if (_pos + 4 > _length) {
            _error = true;
            return 0;
        }
        uint32_t value = _buffer[_pos] |
                        (static_cast<uint32_t>(_buffer[_pos + 1]) << 8) |
                        (static_cast<uint32_t>(_buffer[_pos + 2]) << 16) |
                        (static_cast<uint32_t>(_buffer[_pos + 3]) << 24);
        _pos += 4;
        return value;
    }

    // Read float32 (IEEE 754, little-endian)
    float readFloat32() {
        uint32_t bits = readUint32();
        float value;
        memcpy(&value, &bits, sizeof(value));
        return value;
    }

    // Read bool (single byte)
    bool readBool() {
        return readByte() != 0;
    }

    /**
     * Read propid (1-2 byte property/function ID)
     *
     * Format:
     *   0-127:     1 byte  [0xxxxxxx]
     *   128-32767: 2 bytes [1xxxxxxx] [xxxxxxxx]
     *
     * Returns ID, sets error flag on failure
     */
    uint16_t readPropId() {
        if (_pos >= _length) {
            _error = true;
            return 0;
        }
        uint8_t b0 = _buffer[_pos++];
        if ((b0 & 0x80) == 0) {
            // Single byte: 0-127
            return b0;
        }
        // Two bytes: 128-32767
        if (_pos >= _length) {
            _error = true;
            return 0;
        }
        uint8_t b1 = _buffer[_pos++];
        return (b0 & 0x7F) | (static_cast<uint16_t>(b1) << 7);
    }

    /**
     * Read ident (length-prefixed ASCII identifier)
     *
     * Returns pointer to string in buffer (NOT null-terminated!)
     * Sets outLen to string length
     * Returns nullptr on error
     */
    const char* readIdent(size_t& outLen) {
        if (_pos >= _length) {
            _error = true;
            outLen = 0;
            return nullptr;
        }
        uint8_t len = _buffer[_pos++];
        if (_pos + len > _length) {
            _error = true;
            outLen = 0;
            return nullptr;
        }
        const char* str = reinterpret_cast<const char*>(_buffer + _pos);
        _pos += len;
        outLen = len;
        return str;
    }

    /**
     * Read utf8 string (varint length + UTF-8 bytes)
     *
     * Returns pointer to string in buffer (NOT null-terminated!)
     * Sets outLen to string length
     * Returns nullptr on error
     */
    const char* readUtf8(size_t& outLen) {
        uint32_t len = readVarint();
        if (_error || _pos + len > _length) {
            _error = true;
            outLen = 0;
            return nullptr;
        }
        const char* str = reinterpret_cast<const char*>(_buffer + _pos);
        _pos += len;
        outLen = len;
        return str;
    }

    /**
     * Read blob (varint length + raw bytes)
     *
     * Returns pointer to data in buffer
     * Sets outLen to blob length
     * Returns nullptr on error
     */
    const uint8_t* readBlob(size_t& outLen) {
        uint32_t len = readVarint();
        if (_error || _pos + len > _length) {
            _error = true;
            outLen = 0;
            return nullptr;
        }
        const uint8_t* data = _buffer + _pos;
        _pos += len;
        outLen = len;
        return data;
    }

private:
    const uint8_t* _buffer;
    size_t _length;
    size_t _pos;
    bool _error;
};

} // namespace MicroProto

#endif // MICROPROTO_WIRE_BUFFER_H