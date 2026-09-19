#include "Core/World/Biomes/MountainsBiome.hpp"

#include "Core/Blocks/Blocks.hpp"

namespace kc {
    MountainsBiome::MountainsBiome() {
        terrainSettings.baseHeight = 12.0f;
        terrainSettings.amplitude = 24.0f;
        terrainSettings.noise.frequency = 0.007f;
        terrainSettings.noise.octaves = 5;
        terrainSettings.noise.gain = 0.60f;
        terrainSettings.noise.seedOffset = 303;
        terrainSettings.layers = {
            {Blocks::GRASS_BLOCK, 1}, // surface cap
            {Blocks::STONE,       0}, // fill to bottom
        };
    }
}