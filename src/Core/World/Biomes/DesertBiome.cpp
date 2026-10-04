#include "Core/World/Biomes/DesertBiome.hpp"

#include "Core/Blocks/Blocks.hpp"

namespace kc {
    DesertBiome::DesertBiome() {
        layers = {
            {Blocks::SAND,       2}, // surface dunes
            {Blocks::SANDSTONE,  0}, // fill to bottom
        };
    }
}