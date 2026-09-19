#pragma once

#include "Core/World/TerrainGenSettings.hpp"
#include <vector>

namespace kc {

    // Per-biome noise shaping. Compile-time landscape config: combined with the
    // world seed (+ seedOffset) these knobs give each biome its own character.
    struct BiomeNoiseSettings {
        float frequency = 0.01f;
        int octaves = 4;
        float lacunarity = 2.0f;
        float gain = 0.5f;
        int seedOffset = 0; // varies the shared world seed per biome
    };

    struct BiomeTerrainSettings {
        float baseHeight = 8.0f;   // vertical offset: how high this biome sits
        float amplitude = 10.0f;   // scales the biome noise: how rolling it is

        BiomeNoiseSettings noise; // shape of this biome's terrain

        // Block composition, top→bottom. Reuses the existing Layer type.
        std::vector<TerrainGenSettings::Layer> layers = {}; // Empty
    };

    class Biome {
    protected:
        BiomeTerrainSettings terrainSettings;

    public:
        Biome() = default;
        virtual ~Biome() = default;

        const BiomeTerrainSettings& getTerrainSettings() const {
            return terrainSettings;
        }
    };

}