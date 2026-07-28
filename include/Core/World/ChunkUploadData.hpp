#pragma once
#include <vector>

#include "Chunk.hpp"

namespace lve {
    struct ChunkUploadData {
        uint64_t chunkKey;

        // Data
        std::vector<ChunkVertex> vertices;
        std::vector<uint16_t> indices;
    };
}
