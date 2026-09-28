#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// Canonical, reproducible 64-bit hashing of explicitly encoded fields. Integers
// are fed as little-endian values of a declared width, floats as bit patterns.
// Never feed host pointers, container storage, padding or wall-clock values.
namespace sbk::savestate {
static_assert(std::endian::native == std::endian::little, "canonical encoding assumes a little-endian host");

class Hasher {
    static constexpr uint64_t k0 = 0x9E3779B97F4A7C15ull, k1 = 0xC2B2AE3D27D4EB4Full;
    uint64_t state_ = 0x243F6A8885A308D3ull, length_ = 0;
    static uint64_t mix(uint64_t value) {
        value ^= value >> 31; value *= k1;
        value ^= value >> 29; value *= k0;
        return value ^ (value >> 32);
    }
    void word(uint64_t value) {
        state_ = (state_ ^ mix(value + k0)) * k1;
        state_ = std::rotl(state_, 27) + k0;
    }

public:
    void u8(uint8_t value) { word(value); ++length_; }
    void u16(uint16_t value) { word(value); length_ += 2; }
    void u32(uint32_t value) { word(value); length_ += 4; }
    void u64(uint64_t value) { word(value); length_ += 8; }
    void i32(int32_t value) { u32(static_cast<uint32_t>(value)); }
    void i64(int64_t value) { u64(static_cast<uint64_t>(value)); }
    void boolean(bool value) { u8(value ? 1 : 0); }
    void bytes(const void* data, size_t size) {
        auto* p = static_cast<const uint8_t*>(data);
        u64(size);
        size_t i = 0;
        for (; i + 8 <= size; i += 8) {
            uint64_t value;
            std::memcpy(&value, p + i, 8);
            word(value);
        }
        uint64_t tail = 0;
        if (size > i) std::memcpy(&tail, p + i, size - i);
        word(tail ^ (uint64_t(size - i) << 56));
        length_ += size;
    }
    uint64_t finish() const { return mix(state_ ^ length_ ^ k1); }
};

// Same field sequence as Hasher, emitted as bytes (tests inspect the format).
class ByteSink {
public:
    std::vector<uint8_t>* out;
    template<class T> void put(T value) {
        uint8_t raw[sizeof(T)];
        std::memcpy(raw, &value, sizeof(T));
        out->insert(out->end(), raw, raw + sizeof(T));
    }
    void u8(uint8_t v) { put(v); }
    void u16(uint16_t v) { put(v); }
    void u32(uint32_t v) { put(v); }
    void u64(uint64_t v) { put(v); }
    void i32(int32_t v) { put(v); }
    void i64(int64_t v) { put(v); }
    void boolean(bool v) { u8(v ? 1 : 0); }
    void bytes(const void* data, size_t size) {
        u64(size);
        auto* p = static_cast<const uint8_t*>(data);
        out->insert(out->end(), p, p + size);
    }
};
}
