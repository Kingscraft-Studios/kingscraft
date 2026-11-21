#pragma once

#ifndef KCLINK_INTERNAL_COMPILATION
#error "KCLink internal header: not part of the public API"
#endif

#include <cstddef>
#include <cstdint>

namespace kc::link::detail {

    void writeU32LE(std::uint8_t* destination, std::uint32_t value) noexcept;
    void writeU64LE(std::uint8_t* destination, std::uint64_t value) noexcept;
    std::uint32_t readU32LE(const std::uint8_t* source) noexcept;
    std::uint64_t readU64LE(const std::uint8_t* source) noexcept;

}
