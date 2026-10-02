#pragma once

#include "Core/RegistryKey.hpp"
#include "Core/World/Biomes/Biomes.hpp"
#include "Core/World/TerrainGenSettings.hpp"
#include "FastNoiseLite.h"
#include <cmath>
#include <mutex>

namespace kc {

    class Biome;

    // Continuous membership weights for the 4-biome climate model. All entries
    // are smooth functions of the climate fields, so blending the terrain with
    // these weights is C0-continuous (no sheer cliffs at biome borders).
    struct ClimateWeights {
        float desert = 0.0f;    // hot & dry
        float grassland = 0.0f; // temperate remainder
        float forest = 0.0f;    // wet
        float mountain = 0.0f;  // inland & rugged (overlay)
    };

    // Decides which Biome exists at a world X/Z coordinate, based on a
    // Minecraft-style climate plane (temperature x humidity) plus a
    // continentals x erosion filter for mountains.
    class BiomeProvider {
    public:
        virtual ~BiomeProvider() = default;
        virtual RegistryKey<Biome> getBiome(int worldX, int worldZ) const = 0;
        // Re-seed from an updated settings struct (e.g. a world.kcw load).
        // Default no-op for providers with no seed dependency.
        virtual void applySettings(const TerrainGenSettings&) {}

        // The four placement-only climate fields, normalized to [0,1]. They pick
        // the biome (via getClimateWeights) but never shape terrain height.
        virtual float getTemperature(int worldX, int worldZ) const { return 0.0f; }
        virtual float getHumidity(int worldX, int worldZ) const { return 0.0f; }
        virtual float getContinentalness(int worldX, int worldZ) const { return 0.0f; }
        virtual float getErosion(int worldX, int worldZ) const { return 0.0f; }

        // Continuous biome membership weights used to cross-fade terrain shapes
        // at biome borders. Default zeroes for non-noise providers.
        virtual ClimateWeights getClimateWeights(int worldX, int worldZ) const { return {}; }
    };

}