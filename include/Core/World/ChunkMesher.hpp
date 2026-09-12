#pragma once

#include "Core/World/Chunk.hpp"
#include <cstdint>
#include <vector>

namespace kc {

    class ChunkMesher {
    public:
        static void generateSubChunk(
            SubChunk& subChunk,
            const std::vector<uint64_t>& blockIds,
            int N, int height,
            int yBase,
            const std::vector<uint64_t>* edgePosX = nullptr,
            const std::vector<uint64_t>* edgeNegX = nullptr,
            const std::vector<uint64_t>* edgePosZ = nullptr,
            const std::vector<uint64_t>* edgeNegZ = nullptr);

        // Emits ONLY the faces of subChunk pointing in the given gate direction
        // (2=PosZ, 3=NegZ, 4=PosX, 5=NegX). Used for border re-culling: when a
        // neighbor chunk appears or disappears, only the faces looking into it
        // change, so we re-emit that single plane into the existing mesh instead
        // of rebuilding all 25 sub-chunks. Appends to subChunk.vertices/indices.
        static void emitGateFaces(
            SubChunk& subChunk,
            const std::vector<uint64_t>& blockIds,
            int N, int height,
            int yBase,
            int gate,
            const std::vector<uint64_t>* edgePosX = nullptr,
            const std::vector<uint64_t>* edgeNegX = nullptr,
            const std::vector<uint64_t>* edgePosZ = nullptr,
            const std::vector<uint64_t>* edgeNegZ = nullptr);
    };

} // namespace kc
