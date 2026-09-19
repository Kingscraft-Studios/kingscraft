#pragma once

#include "Core/Blocks/Block.hpp"
#include "Core/RegistryKey.hpp"
#include <vector>

namespace kc {

    struct TerrainGenSettings {
        struct Layer {
            RegistryKey<Block> block;
            int depth; // layers thick; 0 = fill to bottom
        };

        int seed = 1337;
        float frequency = 0.01f;
        int octaves = 4;
        float lacunarity = 2.0f;
        float gain = 0.5f;
    };

} // namespace kc