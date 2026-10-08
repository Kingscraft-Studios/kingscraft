#include "Core/World/Biomes/Biomes.hpp"

#include "Core/World/Biomes/GrasslandsBiome.hpp"
#include "Core/World/Biomes/ForestBiome.hpp"
#include "Core/World/Biomes/DesertBiome.hpp"
#include "Core/World/Biomes/MountainsBiome.hpp"
#include "Core/World/Biomes/OceanBiome.hpp"
#include "Core/World/Biomes/RiverBiome.hpp"

namespace kc {
    void Biomes::registerBiomes(int& pending) {
        auto& registry = Registry<Biome>::getRegistry();

        registry.add(GRASSLANDS, std::make_unique<GrasslandsBiome>());
        registry.add(FOREST, std::make_unique<ForestBiome>());
        registry.add(DESERT, std::make_unique<DesertBiome>());
        registry.add(MOUNTAINS, std::make_unique<MountainsBiome>());
        registry.add(OCEAN, std::make_unique<OceanBiome>());
        registry.add(RIVER, std::make_unique<RiverBiome>());
    }
}
