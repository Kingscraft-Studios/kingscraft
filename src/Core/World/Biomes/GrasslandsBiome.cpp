#include "Core/World/Biomes/GrasslandsBiome.hpp"

#include "Core/Blocks/Blocks.hpp"

namespace kc {
    GrasslandsBiome::GrasslandsBiome() {
        terrainSettings.layers = {
            {Blocks::GRASS_BLOCK, 1}, // surface
            {Blocks::DIRT,        2}, // below
            {Blocks::STONE,       0}, // fill to bottom
        };
    }
}
