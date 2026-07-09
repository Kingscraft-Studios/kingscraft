#pragma once

#include "Core/World/Chunk.hpp"
#include <cstdint>
#include <vector>

namespace lve {

    class ChunkMesher {
    public:
        static void generateSubChunk(
            SubChunk& subChunk,
            const std::vector<uint8_t>& blockIds,
            int N, int height,
            int yBase,
            const std::vector<uint8_t>* edgePosX = nullptr,
            const std::vector<uint8_t>* edgeNegX = nullptr,
            const std::vector<uint8_t>* edgePosZ = nullptr,
            const std::vector<uint8_t>* edgeNegZ = nullptr);
    };

} // namespace lve
