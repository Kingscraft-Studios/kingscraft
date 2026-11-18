#pragma once

#include "Core/World/TerrainGenSettings.hpp"
#include "Core/World/BlockGrid.hpp"
#include <cstdint>
#include <vector>
#include <memory>

namespace kc {

class ITerrainGenerator {
public:
    virtual ~ITerrainGenerator() = default;
    virtual std::vector<uint64_t> generateBlocks(
        int gridX, int gridZ, int chunkSize, int height) = 0;
    virtual std::shared_ptr<BlockGrid> generateBlocksGrid(
        int gridX, int gridZ, int chunkSize, int height) { return nullptr; }
    // Re-seed/re-tune from an updated settings struct, applied after an async
    // world.kcw load. Default no-op for generators that read settings once.
    virtual void applySettings(const TerrainGenSettings&) {}
};

} // namespace kc
