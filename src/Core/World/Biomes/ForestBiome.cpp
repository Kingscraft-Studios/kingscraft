#include "Core/World/Biomes/ForestBiome.hpp"

#include "Core/Blocks/Blocks.hpp"

namespace kc {
    ForestBiome::ForestBiome() {
        terrainSettings.baseHeight = 36.0f;
        terrainSettings.amplitude = 12.0f;
        terrainSettings.noise.frequency = 0.012f;
        terrainSettings.noise.octaves = 4;
        terrainSettings.noise.gain = 0.45f;
        terrainSettings.noise.seedOffset = 101;
        terrainSettings.layers = {
            {Blocks::FOREST_GRASS, 1}, // surface
            {Blocks::DIRT,         3}, // below
            {Blocks::STONE,        0}, // fill to bottom
        };
    }
}