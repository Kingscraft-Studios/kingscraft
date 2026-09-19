#include "Core/World/Biomes/Biomes.hpp"

#include "Core/World/Biomes/GrasslandsBiome.hpp"
#include "Core/World/Biomes/ForestBiome.hpp"
#include "Core/World/Biomes/DesertBiome.hpp"
#include "Core/World/Biomes/MountainsBiome.hpp"

namespace kc {
    void Biomes::registerBiomes() {
        auto& registry = Registry<Biome>::getRegistry();

        registry.add(GRASSLANDS, std::make_unique<GrasslandsBiome>());
        registry.add(FOREST, std::make_unique<ForestBiome>());
        registry.add(DESERT, std::make_unique<DesertBiome>());
        registry.add(MOUNTAINS, std::make_unique<MountainsBiome>());
    }
}
