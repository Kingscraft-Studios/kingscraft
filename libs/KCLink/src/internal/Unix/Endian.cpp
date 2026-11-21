#include "internal/Unix/Endian.hpp"

namespace kc::link::detail {

    void writeU32LE(std::uint8_t* destination, std::uint32_t value) noexcept {
        destination[0] = static_cast<std::uint8_t>(value);
        destination[1] = static_cast<std::uint8_t>(value >> 8);
        destination[2] = static_cast<std::uint8_t>(value >> 16);
        destination[3] = static_cast<std::uint8_t>(value >> 24);
    }

    void writeU64LE(std::uint8_t* destination, std::uint64_t value) noexcept {
        for (std::size_t index = 0; index < 8; ++index)
            destination[index] = static_cast<std::uint8_t>(value >> (index * 8));
    }

    std::uint32_t readU32LE(const std::uint8_t* source) noexcept {
        return static_cast<std::uint32_t>(source[0])
             | (static_cast<std::uint32_t>(source[1]) << 8)
             | (static_cast<std::uint32_t>(source[2]) << 16)
             | (static_cast<std::uint32_t>(source[3]) << 24);
    }

    std::uint64_t readU64LE(const std::uint8_t* source) noexcept {
        std::uint64_t value = 0;
        for (std::size_t index = 0; index < 8; ++index)
            value |= static_cast<std::uint64_t>(source[index]) << (index * 8);
        return value;
    }

}
