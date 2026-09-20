#include "Core/World/Biomes/MountainsBiome.hpp"

#include "Core/Blocks/Blocks.hpp"

namespace kc {
    MountainsBiome::MountainsBiome() {
        terrainSettings.baseHeight = 43.0f;
        terrainSettings.amplitude = 20.0f;
        terrainSettings.noise.frequency = 0.005f;
        terrainSettings.noise.octaves = 5;
        terrainSettings.noise.gain = 0.60f;
        terrainSettings.noise.seedOffset = 303;
        terrainSettings.layers = {
            {Blocks::GRASS_BLOCK, 1}, // surface cap
            {Blocks::STONE,       0}, // fill to bottom
        };
    }
}