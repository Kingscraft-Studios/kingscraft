#pragma once
#include <cstdint>

namespace lve {
    inline uint64_t makeChunkKey(int gx, int gz) {
        return (static_cast<uint64_t>(static_cast<int64_t>(gx)) << 32) |
               (static_cast<uint64_t>(static_cast<int64_t>(gz)) & 0xFFFFFFFF);
    }
} // namespace lve
