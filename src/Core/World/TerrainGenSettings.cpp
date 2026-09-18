#include "Core/World/TerrainGenSettings.hpp"
#include "Core/Blocks/Blocks.hpp"

namespace kc {

    TerrainGenSettings::TerrainGenSettings()
        : layers{
            {Blocks::GRASS_BLOCK, 1}, // surface
            {Blocks::DIRT,        2}, // below
            {Blocks::STONE,       0}, // fill to bottom
        } {}

} // namespace kc