#include "Core/World/Biomes/ForestBiome.hpp"

#include "Core/Blocks/Blocks.hpp"

namespace kc {
    ForestBiome::ForestBiome() {
        layers = {
            {Blocks::FOREST_GRASS, 1}, // surface
            {Blocks::DIRT,         3}, // below
            {Blocks::STONE,        0}, // fill to bottom
        };
    }
}