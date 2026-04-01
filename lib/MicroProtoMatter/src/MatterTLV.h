#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace matter {

// ---------------------------------------------------------------------------
// TLV Element Types
// ---------------------------------------------------------------------------
enum TLVType : uint8_t {
    kTLVInt8    = 0x00, kTLVInt16   = 0x01, kTLVInt32   = 0x02, kTLVInt64   = 0x03,
    kTLVUInt8   = 0x04, kTLVUInt16  = 0x05, kTLVUInt32  = 0x06, kTLVUInt64  = 0x07,
    kTLVFalse   = 0x08, kTLVTrue    = 0x09,
    kTLVFloat32 = 0x0A, kTLVFloat64 = 0x0B,
    kTLVUtf8_1  = 0x0C, kTLVUtf8_2  = 0x0D, kTLVUtf8_4  = 0x0E, kTLVUtf8_8  = 0x0F,
    kTLVBytes1  = 0x10, kTLVBytes2  = 0x11, kTLVBytes4  = 0x12, kTLVBytes8  = 0x13,
    kTLVNull    = 0x14,
    kTLVStruct  = 0x15, kTLVArray   = 0x16, kTLVList    = 0x17,
    kTLVEnd     = 0x18,
};

// Tag forms (upper 3 bits of control byte)
static constexpr uint8_t kTagAnonymous = 0x00;
static constexpr uint8_t kTagContext   = 0x20;
// Higher tag forms exist but we only need anonymous + context-specific

// Sentinel: "no tag" for anonymous elements
static constexpr uint8_t kAnon = 0xFF;

// ---------------------------------------------------------------------------
// TLVWriter – writes Matter TLV into a flat buffer
// ---------------------------------------------------------------------------
class TLVWriter {
    uint8_t* _buf;
    size_t   _cap;
    size_t   _pos = 0;
    bool     _err = false;

    void w8(uint8_t v)   { if (_pos < _cap) _buf[_pos++] = v; else _err = true; }
    void w16(uint16_t v) { w8(v & 0xFF); w8(v >> 8); }
    void w32(uint32_t v) { w16(v & 0xFFFF); w16(v >> 16); }
    void w64(uint64_t v) { w32((uint32_t)v); w32((uint32_t)(v >> 32)); }

    void wtag(uint8_t tag, uint8_t type) {
        if (tag == kAnon) w8(kTagAnonymous | type);
        else { w8(kTagContext | type); w8(tag); }
    }

public:
    TLVWriter(uint8_t* buf, size_t cap) : _buf(buf), _cap(cap) {}

    size_t        size()     const { return _pos; }
    bool          error()    const { return _err; }
    const uint8_t* data()    const { return _buf; }
    uint8_t*      cursor()         { return _buf + _pos; }

    // Reset writer position (useful for re-encoding)
    void reset() { _pos = 0; _err = false; }

    // Containers
    void openStruct(uint8_t tag = kAnon)  { wtag(tag, kTLVStruct); }
    void openArray(uint8_t tag = kAnon)   { wtag(tag, kTLVArray);  }
    void openList(uint8_t tag = kAnon)    { wtag(tag, kTLVList);   }
    void closeContainer()                  { w8(kTLVEnd); }

    // Scalars
    void putBool(uint8_t tag, bool v)      { wtag(tag, v ? kTLVTrue : kTLVFalse); }
    void putNull(uint8_t tag)              { wtag(tag, kTLVNull); }
    void putU8(uint8_t tag, uint8_t v)     { wtag(tag, kTLVUInt8);  w8(v); }
    void putU16(uint8_t tag, uint16_t v)   { wtag(tag, kTLVUInt16); w16(v); }
    void putU32(uint8_t tag, uint32_t v)   { wtag(tag, kTLVUInt32); w32(v); }
    void putU64(uint8_t tag, uint64_t v)   { wtag(tag, kTLVUInt64); w64(v); }
    void putI8(uint8_t tag, int8_t v)      { wtag(tag, kTLVInt8);   w8((uint8_t)v); }
    void putI16(uint8_t tag, int16_t v)    { wtag(tag, kTLVInt16);  w16((uint16_t)v); }
    void putI32(uint8_t tag, int32_t v)    { wtag(tag, kTLVInt32);  w32((uint32_t)v); }
    void putFloat(uint8_t tag, float v)    { wtag(tag, kTLVFloat32); uint32_t b; memcpy(&b, &v, 4); w32(b); }

    void putString(uint8_t tag, const char* s) {
        size_t n = strlen(s);
        if (n <= 0xFF)   { wtag(tag, kTLVUtf8_1); w8((uint8_t)n); }
        else             { wtag(tag, kTLVUtf8_2); w16((uint16_t)n); }
        for (size_t i = 0; i < n && !_err; i++) w8(s[i]);
    }
    void putBytes(uint8_t tag, const uint8_t* d, size_t n) {
        if (n <= 0xFF)   { wtag(tag, kTLVBytes1); w8((uint8_t)n); }
        else             { wtag(tag, kTLVBytes2); w16((uint16_t)n); }
        for (size_t i = 0; i < n && !_err; i++) w8(d[i]);
    }

    // Raw bytes (no TLV framing – for pre-encoded data)
    void putRaw(const uint8_t* d, size_t n) {
        for (size_t i = 0; i < n && !_err; i++) w8(d[i]);
    }

    // Reserve space and return pointer (caller fills in later)
    uint8_t* reserve(size_t n) {
        if (_pos + n > _cap) { _err = true; return nullptr; }
        uint8_t* p = _buf + _pos;
        _pos += n;
        return p;
    }
};

// ---------------------------------------------------------------------------
// TLVReader – reads Matter TLV from a buffer
// ---------------------------------------------------------------------------
class TLVReader {
    const uint8_t* _buf;
    size_t _size;
    size_t _pos = 0;
    bool   _err = false;

    uint8_t _ctrl   = 0;
    uint8_t _tag    = kAnon;
    size_t  _vStart = 0;
    size_t  _vLen   = 0;

    uint8_t  r8()  { if (_pos < _size) return _buf[_pos++]; _err = true; return 0; }
    uint16_t r16() { uint16_t a = r8(); return a | ((uint16_t)r8() << 8); }
    uint32_t r32() { uint32_t a = r16(); return a | ((uint32_t)r16() << 16); }
    uint64_t r64() { uint64_t a = r32(); return a | ((uint64_t)r32() << 32); }

public:
    TLVReader(const uint8_t* buf, size_t size) : _buf(buf), _size(size) {}

    bool next() {
        if (_err || _pos >= _size) return false;
        _ctrl = r8();

        // Tag
        uint8_t tf = _ctrl & 0xE0;
        if (tf == kTagContext) _tag = r8();
        else {
            _tag = kAnon;
            // Skip wider tag forms (rare in Matter protocol messages)
            if (tf == 0x40 || tf == 0x80) { r16(); }
            else if (tf == 0x60 || tf == 0xA0) { r32(); }
            else if (tf == 0xC0) { r16(); r32(); }
            else if (tf == 0xE0) { r32(); r32(); }
        }

        uint8_t et = _ctrl & 0x1F;
        _vStart = _pos;
        _vLen = 0;

        switch (et) {
            case kTLVInt8:  case kTLVUInt8:  _vLen = 1; _pos += 1; break;
            case kTLVInt16: case kTLVUInt16: _vLen = 2; _pos += 2; break;
            case kTLVInt32: case kTLVUInt32: case kTLVFloat32: _vLen = 4; _pos += 4; break;
            case kTLVInt64: case kTLVUInt64: case kTLVFloat64: _vLen = 8; _pos += 8; break;
            case kTLVFalse: case kTLVTrue: case kTLVNull: break;
            case kTLVUtf8_1: case kTLVBytes1:
                _vLen = r8();  _vStart = _pos; _pos += _vLen; break;
            case kTLVUtf8_2: case kTLVBytes2:
                _vLen = r16(); _vStart = _pos; _pos += _vLen; break;
            case kTLVUtf8_4: case kTLVBytes4:
                _vLen = r32(); _vStart = _pos; _pos += _vLen; break;
            case kTLVStruct: case kTLVArray: case kTLVList:
            case kTLVEnd:
                break;
            default: return false;
        }
        return _pos <= _size;
    }

    // Current element info
    uint8_t tag()  const { return _tag; }
    uint8_t type() const { return _ctrl & 0x1F; }

    bool isStruct() const { return type() == kTLVStruct; }
    bool isArray()  const { return type() == kTLVArray; }
    bool isList()   const { return type() == kTLVList; }
    bool isEnd()    const { return type() == kTLVEnd; }
    bool error()    const { return _err; }

    // Value getters
    bool     getBool()   const { return type() == kTLVTrue; }
    uint8_t  getU8()     const { return _buf[_vStart]; }
    uint16_t getU16()    const { return _buf[_vStart] | ((uint16_t)_buf[_vStart+1] << 8); }
    uint32_t getU32()    const {
        return _buf[_vStart] | ((uint32_t)_buf[_vStart+1] << 8) |
               ((uint32_t)_buf[_vStart+2] << 16) | ((uint32_t)_buf[_vStart+3] << 24);
    }
    uint64_t getU64()    const {
        uint64_t lo = _buf[_vStart] | ((uint32_t)_buf[_vStart+1] << 8) |
                      ((uint32_t)_buf[_vStart+2] << 16) | ((uint32_t)_buf[_vStart+3] << 24);
        uint64_t hi = _buf[_vStart+4] | ((uint32_t)_buf[_vStart+5] << 8) |
                      ((uint32_t)_buf[_vStart+6] << 16) | ((uint32_t)_buf[_vStart+7] << 24);
        return lo | (hi << 32);
    }
    int32_t getI32() const { return (int32_t)getU32(); }
    float getFloat() const { float f; memcpy(&f, _buf + _vStart, 4); return f; }

    const uint8_t* getBytes(size_t& len) const { len = _vLen; return _buf + _vStart; }
    size_t getLen() const { return _vLen; }

    // Navigation
    size_t pos() const { return _pos; }

    void skipContainer() {
        int depth = 1;
        while (depth > 0 && next()) {
            uint8_t t = type();
            if (t == kTLVStruct || t == kTLVArray || t == kTLVList) depth++;
            else if (t == kTLVEnd) depth--;
        }
    }
};

} // namespace matter
