#pragma once
#include "Biome.hpp"
#include "Core/RegistryKey.hpp"
#include "Core/Registry.hpp"

namespace kc {

    class Biomes {
    public:
        inline static const RegistryKey<Biome> GRASSLANDS = Registry<Biome>::getRegistry().createKey(ResourceLocation::withDefaultNamespace("grasslands"));
        inline static const RegistryKey<Biome> FOREST = Registry<Biome>::getRegistry().createKey(ResourceLocation::withDefaultNamespace("forest"));
        inline static const RegistryKey<Biome> DESERT = Registry<Biome>::getRegistry().createKey(ResourceLocation::withDefaultNamespace("desert"));
        inline static const RegistryKey<Biome> MOUNTAINS = Registry<Biome>::getRegistry().createKey(ResourceLocation::withDefaultNamespace("mountains"));

        static void registerBiomes();
    };
}
