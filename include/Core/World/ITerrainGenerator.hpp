#pragma once

#include <cstdint>
#include <vector>

namespace lve {

class ITerrainGenerator {
public:
    virtual ~ITerrainGenerator() = default;
    virtual std::vector<uint8_t> generateBlocks(
        int gridX, int gridZ, int chunkSize, int height) = 0;
};

} // namespace lve
