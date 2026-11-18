#pragma once
#include <vector>

#include "Chunk.hpp"

namespace kc {
    struct ChunkUploadData {
        uint64_t chunkKey;

        // Data (opaque pass)
        std::vector<ChunkVertex> vertices;
        std::vector<uint16_t> indices;

        // Data (transparent pass: water/glass drawn at 60% after opaque)
        std::vector<ChunkVertex> transparentVertices;
        std::vector<uint16_t> transparentIndices;
    };
}
