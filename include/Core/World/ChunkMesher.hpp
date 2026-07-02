#pragma once

#include "Core/World/Chunk.hpp"
#include <cstdint>
#include <vector>

namespace lve {

    class ChunkMesher {
    public:
        static void generate(Chunk& chunk, const std::vector<uint8_t>& blockIds, int height,
                             const std::vector<uint8_t>* edgePosX = nullptr,
                             const std::vector<uint8_t>* edgeNegX = nullptr,
                             const std::vector<uint8_t>* edgePosZ = nullptr,
                             const std::vector<uint8_t>* edgeNegZ = nullptr);
    };

} // namespace lve
