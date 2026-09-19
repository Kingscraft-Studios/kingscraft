#include "Core/World/Biomes/DesertBiome.hpp"

#include "Core/Blocks/Blocks.hpp"

namespace kc {
    DesertBiome::DesertBiome() {
        terrainSettings.baseHeight = 7.0f;
        terrainSettings.amplitude = 4.0f;
        terrainSettings.noise.frequency = 0.015f;
        terrainSettings.noise.octaves = 3;
        terrainSettings.noise.gain = 0.35f;
        terrainSettings.noise.seedOffset = 202;
        terrainSettings.layers = {
            {Blocks::SAND,       2}, // surface dunes
            {Blocks::SANDSTONE,  0}, // fill to bottom
        };
    }
}