#include "Core/Blocks/Blocks.hpp"

#include "Core/Blocks/AirBlock.hpp"
#include "Core/Blocks/GrassBlock.hpp"
#include "Core/Blocks/StoneBlock.hpp"
#include "Core/Blocks/DirtBlock.hpp"
#include "Core/Blocks/SandBlock.hpp"
#include "Core/Blocks/SandstoneBlock.hpp"
#include "Core/Blocks/GravelBlock.hpp"
#include "Core/Blocks/WaterBlock.hpp"
#include "Threads/Logger.hpp"

namespace kc {

    void Blocks::registerBlocks(int& pending) {
        auto& registry = Registry<Block>::getRegistry();

        auto air = std::make_unique<AirBlock>();
        registry.add(AIR, std::move(air));

        loadBlock<GrassBlock>(GRASS_BLOCK, "resources/models/block/grass_block.json", pending, registry);
        loadBlock<GrassBlock>(FOREST_GRASS, "resources/models/block/forest_grass_block.json", pending, registry);
        loadBlock<StoneBlock>(STONE, "resources/models/block/stone_block.json", pending, registry);
        loadBlock<DirtBlock>(DIRT, "resources/models/block/dirt_block.json", pending, registry);
        loadBlock<SandBlock>(SAND, "resources/models/block/sand_block.json", pending, registry);
        loadBlock<SandstoneBlock>(SANDSTONE, "resources/models/block/sandstone_block.json", pending, registry);
        loadBlock<GravelBlock>(GRAVEL, "resources/models/block/gravel_block.json", pending, registry);
        loadBlock<WaterBlock>(WATER, "resources/models/block/water_block.json", pending, registry);
    }

} // namespace kc