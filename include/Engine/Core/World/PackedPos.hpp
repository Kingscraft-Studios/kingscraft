#pragma once

#include <cstdint>
#include "Core/World/Location.hpp"

namespace kc {

    // Packed position for chunk-local coordinates
    // Assumes chunkSize <= 16 and worldHeight <= 512
    class PackedPos {
    public:
        uint32_t value = 0;

        PackedPos() = default;
        explicit PackedPos(uint32_t v) : value(v) {}

        static PackedPos pack(int x, int y, int z) {
            PackedPos p;
            p.value = (static_cast<uint32_t>(x) & 0xF);
            p.value |= ((static_cast<uint32_t>(z) & 0xF) << 4);
            p.value |= ((static_cast<uint32_t>(y) & 0x1FF) << 8);
            return p;
        }

        static PackedPos pack(const Location& loc) {
            return pack(loc.x, loc.y, loc.z);
        }

        void unpack(int& x, int& y, int& z) const {
            x = value & 0xF;
            z = (value >> 4) & 0xF;
            y = (value >> 8) & 0x1FF;
        }

        Location toLocation() const {
            Location loc;
            unpack(loc.x, loc.y, loc.z);
            return loc;
        }

        operator uint32_t() const { return value; }
    };

} // namespace kc
