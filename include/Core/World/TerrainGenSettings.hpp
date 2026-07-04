#pragma once

#include "Core/Blocks/Blocks.hpp"
#include <vector>

namespace lve {

    class TerrainGenSettings {
    public:
        struct Layer {
            const RegistryKey<Block>* block;
            int depth; // layers thick; 0 = fill to bottom
        };

        static TerrainGenSettings& get() {
            static TerrainGenSettings instance;
            return instance;
        }

        // Noise
        int seed = 1337;
        float frequency = 0.01f;
        float amplitude = 10.0f;
        float baseHeight = 8.0f;
        int octaves = 4;
        float lacunarity = 2.0f;
        float gain = 0.5f;

        // Layers (ordered top -> bottom from surface height)
        std::vector<Layer> layers = {
            {&Blocks::GRASS_BLOCK, 1}, // surface
            {&Blocks::DIRT,        2}, // below
            {&Blocks::STONE,       0}, // fill to bottom
        };

    private:
        TerrainGenSettings() = default;
    };

} // namespace lve
